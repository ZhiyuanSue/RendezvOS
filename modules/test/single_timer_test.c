/*
 * One-shot core timer: EXPIRE via IRQ (wait_port + delivery_token) and CANCEL
 * from thread context.
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/thread_loader.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/time.h>
#include <rendezvos/ipc/ipc.h>
#include <rendezvos/ipc/kmsg.h>
#include <rendezvos/ipc/ipc_serial.h>
#include <rendezvos/ipc/message.h>
#include <rendezvos/ipc/port.h>

#define TIMER_TEST_EXPIRE_TOKEN 0x54494d4552455850ull /* "TIMEREXP" */
#define TIMER_TEST_CANCEL_TOKEN 0x54494d4543414e43ull /* "TIMECANC" */

extern u64 heartbeat_gap;

static volatile int timer_waiter_phase1;
static volatile int timer_waiter_phase2;
static volatile int timer_waiter_failed;

static bool timer_test_parse_kmsg(const Message_t* msg, u16 expect_module,
                                  u16 expect_opcode, u64 expect_token)
{
        const kmsg_t* km;
        i64 token;

        if (!msg)
                return false;
        km = kmsg_from_msg(msg);
        if (!km || km->hdr.module != expect_module
            || km->hdr.opcode != expect_opcode)
                return false;
        if (ipc_serial_decode(km->payload, km->hdr.payload_len,
                              KMSG_FMT_SYSTEM_TIMER, &token)
            != REND_SUCCESS)
                return false;
        return (u64)token == expect_token;
}

static void* timer_waiter_thread(void* arg)
{
        Message_Port_t* port = (Message_Port_t*)arg;
        Message_t* msg;

        if (recv_msg(port) != REND_SUCCESS) {
                pr_error("[single_timer_test] waiter: expire recv failed\n");
                timer_waiter_failed = 1;
                return NULL;
        }
        msg = dequeue_recv_msg();
        if (!timer_test_parse_kmsg(msg, port->service_id,
                                    KMSG_OP_SYSTEM_TIMER_EXPIRE,
                                    TIMER_TEST_EXPIRE_TOKEN)) {
                pr_error("[single_timer_test] waiter: bad TIMER_EXPIRE kmsg\n");
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                timer_waiter_failed = 1;
                return NULL;
        }
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
        timer_waiter_phase1 = 1;

        if (recv_msg(port) != REND_SUCCESS) {
                pr_error("[single_timer_test] waiter: cancel recv failed\n");
                timer_waiter_failed = 1;
                return NULL;
        }
        msg = dequeue_recv_msg();
        if (!timer_test_parse_kmsg(msg, port->service_id,
                                    KMSG_OP_SYSTEM_TIMER_CANCEL,
                                    TIMER_TEST_CANCEL_TOKEN)) {
                pr_error("[single_timer_test] waiter: bad TIMER_CANCEL kmsg\n");
                ref_put(&msg->ms_queue_node.refcount, free_message_ref);
                timer_waiter_failed = 1;
                return NULL;
        }
        ref_put(&msg->ms_queue_node.refcount, free_message_ref);
        timer_waiter_phase2 = 1;
        return NULL;
}

static tick_t timer_test_short_delay(void)
{
        tick_t delta = heartbeat_gap / 10;

        if (delta == 0)
                delta = rendezvos_time_ms_to_count(50);
        if (delta == 0)
                delta = 1000;
        return delta;
}

int single_timer_test(void)
{
        Message_Port_t* port;
        Task_Manager* tm = percpu(core_tm);
        rendezvos_timer_event ev;
        error_t err;
        tick_t now;

        timer_waiter_phase1 = 0;
        timer_waiter_phase2 = 0;
        timer_waiter_failed = 0;
        is_print_sche_info = false;

        port = create_message_port("single_timer_test_port");
        if (!port) {
                pr_error("[single_timer_test] create_message_port failed\n");
                return -E_REND_TEST;
        }

        err = gen_thread_from_func(
                NULL, timer_waiter_thread, "timer_waiter", tm, port);
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] spawn waiter failed\n");
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }

        schedule(tm);

        now = arch_timer_read();
        err = rendezvos_timer_event_init(
                &ev, 0, port, TIMER_TEST_EXPIRE_TOKEN);
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] init expire failed e=%d\n", err);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        err = rendezvos_timer_event_add(&ev, now + timer_test_short_delay());
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] add expire failed e=%d\n", err);
                rendezvos_timer_event_fini(&ev);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }

        while (!timer_waiter_phase1 && !timer_waiter_failed)
                schedule(tm);
        if (timer_waiter_failed || !timer_waiter_phase1) {
                pr_error("[single_timer_test] expire phase failed\n");
                rendezvos_timer_event_fini(&ev);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }

        now = arch_timer_read();
        err = rendezvos_timer_event_init(
                &ev, 0, port, TIMER_TEST_CANCEL_TOKEN);
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] init cancel failed e=%d\n", err);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        err = rendezvos_timer_event_add(
                &ev, now + timer_test_short_delay() * 1000);
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] add cancel-arm failed e=%d\n",
                         err);
                rendezvos_timer_event_fini(&ev);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        err = rendezvos_timer_event_cancel(&ev);
        if (err != REND_SUCCESS) {
                pr_error("[single_timer_test] cancel failed e=%d\n", err);
                rendezvos_timer_event_fini(&ev);
                delete_message_port_structure(port);
                return -E_REND_TEST;
        }
        rendezvos_timer_event_fini(&ev);

        while (!timer_waiter_phase2 && !timer_waiter_failed)
                schedule(tm);

        delete_message_port_structure(port);

        if (timer_waiter_failed || !timer_waiter_phase2) {
                pr_error("[single_timer_test] cancel phase failed\n");
                return -E_REND_TEST;
        }

        return REND_SUCCESS;
}
