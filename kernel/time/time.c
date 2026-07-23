#include <rendezvos/time.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/ipc/kmsg_system.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/system/panic.h>
#include <common/dsa/rb_tree.h>
#include <common/dsa/list.h>
#include <common/string.h>
u64 loop_per_jeffies;
u64 udelay_max_loop;
u64 heartbeat_gap;
u64 clock_hz = 0;
u64 jeffy_ticks;
enum timer_type sys_timer_type = TIMER_TYPE_ONE_SHOT;
DEFINE_PER_CPU(u64, tick_cnt);
DEFINE_PER_CPU(u64, boot_base_time);
DEFINE_PER_CPU(struct rendezvos_timer_event, heartbeat_event);
DEFINE_PER_CPU(struct rb_root, event_tree_root);
i64 jeffies_get(void)
{
        if (!clock_hz)
                return 0;
        return (i64)((arch_timer_read() - per_cpu(boot_base_time, BSP_ID))
                     / jeffy_ticks);
}

static bool timer_event_is_rb_head(const rendezvos_timer_event *event,
                                   const struct rb_root *root)
{
        const struct rb_node *node = &event->node;
        return root->rb_root == node || RB_PARENT(node);
}

static bool timer_event_is_linked(const rendezvos_timer_event *event,
                                  const struct rb_root *root)
{
        struct list_entry *n = (struct list_entry *)&event->same_expired_list;

        return timer_event_is_rb_head(event, root)
               || (list_node_is_valid(n) && !list_node_is_detached(n));
}

/*rb tree ops*/
static rendezvos_timer_event *timer_event_rb_tree_find(struct rb_root *root,
                                                       tick_t expired)
{
        struct rb_node *node = root->rb_root;

        while (node) {
                rendezvos_timer_event *tmp =
                        container_of(node, rendezvos_timer_event, node);

                if (time_before(expired, tmp->expired))
                        node = node->left_child;
                else if (time_before(tmp->expired, expired))
                        node = node->right_child;
                else
                        return tmp;
        }
        return NULL;
}

static error_t timer_event_rb_tree_insert(rendezvos_timer_event *event,
                                          struct rb_root *root)
{
        struct rb_node **new = &root->rb_root, *parent = NULL;
        rendezvos_timer_event *tmp;

        while (*new) {
                parent = *new;
                tmp = container_of(parent, rendezvos_timer_event, node);
                if (time_before(event->expired, tmp->expired))
                        new = &parent->left_child;
                else if (time_before(tmp->expired, event->expired))
                        new = &parent->right_child;
                else
                        return -E_IN_PARAM;
        }
        RB_Link_Node(&event->node, parent, new);
        RB_SolveDoubleRed(&event->node, root);
        return REND_SUCCESS;
}

static void timer_event_rb_tree_remove(rendezvos_timer_event *event,
                                       struct rb_root *root)
{
        RB_Remove(&event->node, root);
        event->node.black_height = event->node.rb_parent_color = 0;
        event->node.left_child = event->node.right_child = NULL;
}

static rendezvos_timer_event *timer_event_rb_tree_first(struct rb_root *root)
{
        struct rb_node *node = root->rb_root;

        if (!node)
                return NULL;
        while (node->left_child)
                node = node->left_child;
        return container_of(node, rendezvos_timer_event, node);
}

static error_t timer_event_deliver_kmsg(Message_Port_t *port, u16 opcode,
                                        u64 delivery_token,
                                        bool use_system_proxy)
{
        Msg_Data_t *msgdata;
        Message_t *msg;

        msgdata = kmsg_create(port->service_id,
                              opcode,
                              KMSG_FMT_SYSTEM_TIMER,
                              (i64)delivery_token);
        if (!msgdata)
                return -E_REND_IPC;
        msg = create_message_with_msg(msgdata);
        ref_put(&msgdata->refcount, free_msgdata_ref_default);
        if (!msg)
                return -E_REND_IPC;
        return ipc_system_try_deliver(port, msg, use_system_proxy);
}

/*timer event ops*/
/*
 * Do not change the following comments:
 * the timer is per-cpu, but some functions in this module still might have race
 * condition. because the syscall is inherit the irq state of user.
 * So, if in a timer syscall(e.x. sleep), it might changing the rb tree,
 * but the irq is still enable. And if the syscall's changing the rb tree, and a
 * timer irq happen, it must be an error.
 *
 * So we must disable the irq(not lock, for it's percpu timer, a irq disable is
 * enough). and then can we read or write the rb tree.
 *
 */
error_t rendezvos_timer_event_init(rendezvos_timer_event *event,
                                   u64 periodic_gap, Message_Port_t *wait_port,
                                   u64 delivery_token)
{
        if (!event)
                return -E_IN_PARAM;
        if (!periodic_gap && !wait_port)
                return -E_IN_PARAM;
        if (periodic_gap && wait_port)
                return -E_IN_PARAM;
        if (!periodic_gap && !ref_get_not_zero(&wait_port->refcount))
                return -E_REND_IPC;

        memset(event, 0, sizeof(*event));
        INIT_LIST_HEAD(&event->same_expired_list);
        event->periodic_gap = periodic_gap;
        event->delivery_token = delivery_token;
        event->wait_port = wait_port;
        return REND_SUCCESS;
}

bool rendezvos_timer_event_exist_inner(const rendezvos_timer_event *event)
{
        return timer_event_is_linked(event, &percpu(event_tree_root));
}

static inline error_t
rendezvos_timer_event_add_inner(rendezvos_timer_event *event, tick_t expires_at)
{
        struct rb_root *root = &percpu(event_tree_root);

        if (timer_event_is_linked(event, root))
                return -E_IN_PARAM;
        if (!event->periodic_gap && !event->wait_port)
                return -E_IN_PARAM;
        event->expired = expires_at;
        rendezvos_timer_event *head =
                timer_event_rb_tree_find(root, event->expired);
        if (head) {
                list_add_tail(&event->same_expired_list,
                              &head->same_expired_list);
                return REND_SUCCESS;
        }
        INIT_LIST_HEAD(&event->same_expired_list);
        return timer_event_rb_tree_insert(event, root);
}
static inline error_t
rendezvos_timer_event_del_inner(rendezvos_timer_event *event)
{
        struct rb_root *root = &percpu(event_tree_root);

        if (timer_event_is_rb_head(event, root)) {
                if (list_empty(&event->same_expired_list)) {
                        timer_event_rb_tree_remove(event, root);
                } else {
                        rendezvos_timer_event *succ =
                                list_entry(event->same_expired_list.next,
                                           rendezvos_timer_event,
                                           same_expired_list);

                        list_del(&event->same_expired_list);

                        timer_event_rb_tree_remove(event, root);
                        timer_event_rb_tree_insert(succ, root);
                }
        } else if (list_node_is_valid(&event->same_expired_list)
                   && !list_node_is_detached(&event->same_expired_list)) {
                list_del_init(&event->same_expired_list);
        } else {
                return -E_REND_NOFOUND;
        }
        return REND_SUCCESS;
}
error_t rendezvos_timer_event_fini(rendezvos_timer_event *event)
{
        if (event->periodic_gap)
                return -E_IN_PARAM;
        u64 flags = arch_save_and_disable_irq();
        if (rendezvos_timer_event_exist_inner(event))
                rendezvos_timer_event_del_inner(event);
        arch_irq_restore(flags);
        if (event->wait_port) {
                ref_put(&event->wait_port->refcount, free_message_port_ref);
                event->wait_port = NULL;
        }
        return REND_SUCCESS;
}

error_t rendezvos_timer_event_cancel(rendezvos_timer_event *event)
{
        if (!event || event->periodic_gap || !event->wait_port)
                return -E_IN_PARAM;

        u64 flags = arch_save_and_disable_irq();
        if (rendezvos_timer_event_exist_inner(event))
                rendezvos_timer_event_del_inner(event);
        arch_irq_restore(flags);
        return timer_event_deliver_kmsg(event->wait_port,
                                        KMSG_OP_SYSTEM_TIMER_CANCEL,
                                        event->delivery_token,
                                        false);
}
error_t rendezvos_timer_event_add(rendezvos_timer_event *event,
                                  tick_t expires_at)
{
        u64 flags = arch_save_and_disable_irq();
        error_t e = rendezvos_timer_event_add_inner(event, expires_at);
        arch_irq_restore(flags);
        return e;
}

error_t rendezvos_timer_event_del(rendezvos_timer_event *event)
{
        u64 flags = arch_save_and_disable_irq();
        error_t e = rendezvos_timer_event_del_inner(event);
        arch_irq_restore(flags);
        return e;
}

error_t rendezvos_timer_event_change(rendezvos_timer_event *event,
                                     tick_t expires_at)
{
        u64 flags = arch_save_and_disable_irq();
        error_t res = rendezvos_timer_event_del_inner(event);
        if (res == REND_SUCCESS)
                res = rendezvos_timer_event_add_inner(event, expires_at);
        arch_irq_restore(flags);
        return res;
}

bool rendezvos_timer_event_exist(const rendezvos_timer_event *event)
{
        u64 flags = arch_save_and_disable_irq();
        bool res = rendezvos_timer_event_exist_inner(event);
        arch_irq_restore(flags);
        return res;
}

/*timer part*/
__attribute__((optimize("O0"))) u64 loop_delay(volatile u64 loop_cnt)
{
        u64 cnt = loop_cnt;
        while (loop_cnt--)
                ;
        return cnt;
}
static inline u64 timer_calibration(void)
{
        volatile u64 lpj = 0;
#define LPJ_CALIBRATION_CNT 25
        if (!clock_hz)
                return 0;
        tick_t gap = clock_hz / INT_PER_SECOND;
        if (!gap)
                gap = 1;
        for (int i = 0; i < LPJ_CALIBRATION_CNT; i++) {
                tick_t start = arch_timer_read();
                tick_t end = start + gap;

                while (time_before(arch_timer_read(), end))
                        lpj++;
        }
        return lpj / LPJ_CALIBRATION_CNT;
}
void rendezvos_time_init(void)
{
        percpu(event_tree_root).rb_root = NULL;
        percpu(tick_cnt) = 0;
        register_irq_handler(
                timer_irq_num, rendezvos_do_time_irq, IRQ_NEED_EOI);
        bool is_bsp = (percpu(cpu_number) == BSP_ID);
        heartbeat_gap = arch_init_timer(is_bsp);

        percpu(boot_base_time) = arch_timer_read();
        error_t err = rendezvos_timer_event_init(
                &percpu(heartbeat_event), heartbeat_gap, NULL, 0);
        if (err != REND_SUCCESS)
                kernel_panic(
                        "[ ERROR ]rendezvos_time_init: heartbeat init failed");

        err = rendezvos_timer_event_add(&percpu(heartbeat_event),
                                        percpu(boot_base_time) + heartbeat_gap);
        if (err != REND_SUCCESS)
                kernel_panic(
                        "[ ERROR ]rendezvos_time_init: heartbeat add failed");

        if (is_bsp) {
                clock_hz = arch_timer_get_hz();
                jeffy_ticks = clock_hz / INT_PER_SECOND;
                if (!jeffy_ticks)
                        jeffy_ticks = 1;
                loop_per_jeffies = timer_calibration();
        }
        udelay_max_loop = (loop_per_jeffies * UDELAY_MAX * UDELAY_MUL)
                          >> UDELAY_SHIFT;
}
void rendezvos_do_time_irq(struct trap_frame *tf)
{
        struct rb_root *root = &percpu(event_tree_root);
        rendezvos_timer_event *current_event;
        rendezvos_timer_event *next_event;
        tick_t now;
        u64 next_event_gap;
        // print("go into do timer irq\n");

        (void)tf;

        /*handle current and some might expired event*/
        now = arch_timer_read();
        while ((current_event = timer_event_rb_tree_first(root))
               && !time_before(now, current_event->expired)) {
                if (current_event->periodic_gap) {
                        rendezvos_timer_event_change(
                                current_event,
                                now + current_event->periodic_gap);
                        if (current_event == &percpu(heartbeat_event)
                            && clock_hz) {
                                percpu(tick_cnt) =
                                        (now - percpu(boot_base_time))
                                        / jeffy_ticks;
                        }
                } else {
                        error_t e = timer_event_deliver_kmsg(
                                current_event->wait_port,
                                KMSG_OP_SYSTEM_TIMER_EXPIRE,
                                current_event->delivery_token,
                                true);
                        if (e == REND_SUCCESS) {
                                rendezvos_timer_event_fini(current_event);
                        } else {
                                rendezvos_timer_event_change(current_event,
                                                             now + 1);
                        }
                }
        }

        now = arch_timer_read();
        next_event = timer_event_rb_tree_first(root);
        if (!next_event) {
                /*impossible,because we must reput the heartbeat into it*/
                arch_reset_timer(heartbeat_gap);
                return;
        }
        if (time_before(now, next_event->expired))
                next_event_gap = next_event->expired - now;
        else
                next_event_gap = 1;
        arch_reset_timer(next_event_gap);
}
tick_t rendezvos_time_now(void)
{
        return arch_timer_read() - percpu(boot_base_time);
}
u64 rendezvos_time_count_to_us(tick_t count)
{
        if (!clock_hz)
                return 0;
        return (count / clock_hz) * 1000000ULL
               + (count % clock_hz) * 1000000ULL / clock_hz;
}
u64 rendezvos_time_count_to_ms(tick_t count)
{
        if (!clock_hz)
                return 0;
        return (count / clock_hz) * 1000ULL
               + (count % clock_hz) * 1000ULL / clock_hz;
}
tick_t rendezvos_time_us_to_count(u64 us)
{
        if (!clock_hz)
                return 0;
        return us * clock_hz / 1000000ULL;
}
tick_t rendezvos_time_ms_to_count(u64 ms)
{
        if (!clock_hz)
                return 0;
        return ms * clock_hz / 1000ULL;
}
void __udelay(u64 lpj, u64 us)
{
        u64 loops = (lpj * us * UDELAY_MUL) >> UDELAY_SHIFT;
        loop_delay(loops);
}
void _udelay(u64 uml, u64 lpj, volatile u64 us)
{
        while (us >= UDELAY_MAX) {
                loop_delay(uml);
                us -= UDELAY_MAX;
        }
        __udelay(lpj, us);
}
void udelay(u64 us)
{
        _udelay(udelay_max_loop, loop_per_jeffies, us);
}
void mdelay(u64 ms)
{
        _udelay(udelay_max_loop, loop_per_jeffies, ms * 1000);
}