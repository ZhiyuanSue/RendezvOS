#include <rendezvos/time.h>
#include <rendezvos/smp/percpu.h>
#include <common/atomic.h>
#include <common/dsa/rb_tree.h>
#include <common/dsa/list.h>
#include <common/string.h>
#include <modules/log/log.h>
volatile i64 jeffies = 0;
u64 loop_per_jeffies;
u64 udelay_max_loop;
u64 heartbeat_gap;
u64 clock_hz;
enum timer_type sys_timer_type = TIMER_TYPE_ONE_SHOT;
DEFINE_PER_CPU(u64, tick_cnt);
DEFINE_PER_CPU(u64, boot_base_time);
DEFINE_PER_CPU(struct rendezvos_timer_event, heartbeat_event);
DEFINE_PER_CPU(struct rb_root, event_tree_root);

static bool timer_event_is_rb_head(const rendezvos_timer_event *event,
                                   const struct rb_root *root)
{
        const struct rb_node *node = &event->node;
        return root->rb_root == node || RB_PARENT(node);
}

static bool timer_event_is_linked(const rendezvos_timer_event *event,
                                  const struct rb_root *root)
{
        return timer_event_is_rb_head(event, root)
               || !list_node_is_detached(
                       (struct list_entry *)&event->same_expired_list);
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

/*timer event ops*/
void rendezvos_timer_event_init(rendezvos_timer_event *event)
{
        memset(event, 0, sizeof(*event));
        INIT_LIST_HEAD(&event->same_expired_list);
}

error_t rendezvos_timer_event_add(rendezvos_timer_event *event,
                                  tick_t expires_at)
{
        struct rb_root *root = &percpu(event_tree_root);

        if (timer_event_is_linked(event, root))
                return -E_IN_PARAM;
        event->expired = expires_at;
        rendezvos_timer_event *head =
                timer_event_rb_tree_find(root, event->expired);
        if (head) {
                list_add_tail(&event->same_expired_list,
                              &head->same_expired_list);
                return REND_SUCCESS;
        } else {
                INIT_LIST_HEAD(&event->same_expired_list);
                return timer_event_rb_tree_insert(event, root);
        }
}

error_t rendezvos_timer_event_del(rendezvos_timer_event *event)
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
        } else if (!list_node_is_detached(&event->same_expired_list)) {
                list_del_init(&event->same_expired_list);
        } else {
                return -E_REND_NOFOUND;
        }
        return REND_SUCCESS;
}

error_t rendezvos_timer_event_change(rendezvos_timer_event *event,
                                     tick_t expires_at)
{
        error_t res = rendezvos_timer_event_del(event);
        if (res != REND_SUCCESS)
                return res;
        return rendezvos_timer_event_add(event, expires_at);
}

bool rendezvos_timer_event_exist(const rendezvos_timer_event *event)
{
        return timer_event_is_linked(event, &percpu(event_tree_root));
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
        i64 tick_val;
#define LPJ_CALIBRATION_CNT 25
        for (int i = 0; i < LPJ_CALIBRATION_CNT; i++) {
                tick_val = jeffies;
                while (tick_val == jeffies)
                        ; /*wait for next jeffies*/
                tick_val = jeffies;

                while (tick_val == jeffies) {
                        lpj++;
                }
        }
        return lpj / 25;
}
void rendezvos_time_init(void)
{
        percpu(event_tree_root).rb_root = NULL;
        percpu(tick_cnt) = jeffies;
        register_irq_handler(
                timer_irq_num, rendezvos_do_time_irq, IRQ_NEED_EOI);
        bool is_bsp = (percpu(cpu_number) == BSP_ID);
        heartbeat_gap = arch_init_timer(is_bsp);

        percpu(boot_base_time) = arch_timer_read();
        rendezvos_timer_event_init(&percpu(heartbeat_event));
        percpu(heartbeat_event).periodic_gap = heartbeat_gap;
        rendezvos_timer_event_add(&percpu(heartbeat_event),
                                  percpu(boot_base_time) + heartbeat_gap);
        if (is_bsp) {
                loop_per_jeffies = timer_calibration();
                clock_hz = arch_timer_get_hz();
        }
        udelay_max_loop = (loop_per_jeffies * UDELAY_MAX * UDELAY_MUL)
                          >> UDELAY_SHIFT;
}
void rendezvos_do_time_irq(struct trap_frame *tf)
{
        u64 local_tick;
        i64 global_tick;
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
                        if (current_event != &percpu(heartbeat_event))
                                continue;
                        local_tick = ++percpu(tick_cnt);
                        global_tick = (i64)atomic64_load(
                                (volatile const u64 *)&jeffies);
                        while (time_after(local_tick, (u64)global_tick)) {
                                if (atomic64_cas((volatile u64 *)&jeffies,
                                                 (u64)global_tick,
                                                 local_tick)
                                    == (u64)global_tick)
                                        break;
                                global_tick = (i64)atomic64_load(
                                        (volatile const u64 *)&jeffies);
                        }
                } else {
                        rendezvos_timer_event_del(current_event);
                        /* TODO: PORT delivery via timer_delivery kthread */
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