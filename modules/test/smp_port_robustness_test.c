/*
 * SMP Port Robustness Test: Tests port allocation, registration, lookup, and
 * unregistration under heavy concurrent load across multiple CPUs.
 * 
 * This test exercises:
 * 1. Port table operations (register/unregister/lookup) under SMP
 * 2. Port cache behavior under concurrent access
 * 3. Reference counting correctness
 */
#include <modules/test/test.h>
#include <modules/log/log.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/ipc.h>
#include <rendezvos/task/message.h>
#include <rendezvos/task/port.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/kmalloc.h>
#include <common/string.h>
#include <common/stddef.h>

extern u32 BSP_ID;
extern int NR_CPU;

#define SMP_PORT_TEST_ROUNDS 10000
#define SMP_PORT_MAX_PORTS_PER_CPU 10
#define SMP_PORT_NAME_LEN 32

/* Per-CPU test state */
struct smp_port_test_state {
        Message_Port_t* ports[SMP_PORT_MAX_PORTS_PER_CPU];
        char port_names[SMP_PORT_MAX_PORTS_PER_CPU][SMP_PORT_NAME_LEN];
        u32 port_count;
        u64 register_count;
        u64 lookup_count;
        u64 unregister_count;
        u64 error_count;
};

static DEFINE_PER_CPU(struct smp_port_test_state, smp_port_test_state);

/* Helper: copy string */
static void my_strcpy(char* dst, const char* src)
{
        while (*src) {
                *dst++ = *src++;
        }
        *dst = '\0';
}

/* Helper: convert number to string */
static void num_to_str(u64 num, char* buf, u32 buf_len)
{
        if (buf_len == 0)
                return;
        
        if (num == 0) {
                buf[0] = '0';
                buf[1] = '\0';
                return;
        }
        
        char digits[32];
        u32 digit_count = 0;
        u64 n = num;
        
        while (n > 0 && digit_count < 31) {
                digits[digit_count++] = '0' + (n % 10);
                n /= 10;
        }
        
        u32 i = 0;
        for (u32 j = digit_count; j > 0 && i < buf_len - 1; j--) {
                buf[i++] = digits[j - 1];
        }
        buf[i] = '\0';
}

/* Helper: create port name */
static void make_port_name(char* buf, u32 buf_len, u32 cpu_id, u64 round)
{
        my_strcpy(buf, "cpu");
        num_to_str(cpu_id, buf + strlen(buf), buf_len - strlen(buf));
        my_strcpy(buf + strlen(buf), "_r");
        num_to_str(round, buf + strlen(buf), buf_len - strlen(buf));
}

/* Test 1: Port table operations (register/unregister/lookup) */
static int smp_port_table_test(u32 cpu_id)
{
        struct smp_port_test_state* state = &percpu(smp_port_test_state);
        memset(state, 0, sizeof(*state));
        
        if (!global_port_table) {
                pr_error("[smp_port_test] CPU %u: global_port_table is NULL\n", cpu_id);
                return -E_REND_TEST;
        }
        
        for (u32 round = 0; round < SMP_PORT_TEST_ROUNDS; round++) {
                /* Create and register a port */
                char port_name[SMP_PORT_NAME_LEN];
                make_port_name(port_name, sizeof(port_name), cpu_id, round);
                
                Message_Port_t* port = create_message_port(port_name);
                if (!port) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: create_message_port failed at round %u\n",
                                        cpu_id, round);
                        }
                        continue;
                }
                
                error_t e = register_port(global_port_table, port);
                if (e != REND_SUCCESS) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: register_port failed at round %u, error=%d\n",
                                        cpu_id, round, e);
                        }
                        delete_message_port_structure(port);
                        continue;
                }
                state->register_count++;
                
                /* Lookup the port */
                Message_Port_t* found = thread_lookup_port(port_name);
                if (!found || found != port) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: lookup failed at round %u, found=%x, port=%x\n",
                                        cpu_id, round, (u32)(u64)found, (u32)(u64)port);
                        }
                        unregister_port(global_port_table, port_name);
                        ref_put(&port->refcount, free_message_port_ref);
                        continue;
                }
                state->lookup_count++;
                
                /* Unregister */
                e = unregister_port(global_port_table, port_name);
                if (e != REND_SUCCESS) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: unregister failed at round %u, error=%d\n",
                                        cpu_id, round, e);
                        }
                } else {
                        state->unregister_count++;
                }
                
                /* Release reference from lookup */
                ref_put(&found->refcount, free_message_port_ref);
                
                /* Release reference from create */
                ref_put(&port->refcount, free_message_port_ref);
                
                /* Verify port is no longer findable */
                found = thread_lookup_port(port_name);
                if (found != NULL) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: port still findable after unregister at round %u\n",
                                        cpu_id, round);
                        }
                        ref_put(&found->refcount, free_message_port_ref);
                }
                
                if (round % (SMP_PORT_TEST_ROUNDS / 10) == 0 && cpu_id == BSP_ID) {
                        pr_info("[smp_port_test] CPU %u: Round %u/%u\n",
                                cpu_id, round, SMP_PORT_TEST_ROUNDS);
                }
        }
        
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] CPU %u: Table test done: reg=%x lookup=%x unreg=%x errors=%x\n",
                        cpu_id,
                        (u32)(state->register_count & 0xFFFFFFFF),
                        (u32)(state->lookup_count & 0xFFFFFFFF),
                        (u32)(state->unregister_count & 0xFFFFFFFF),
                        (u32)(state->error_count & 0xFFFFFFFF));
        }
        
        return (state->error_count > 0) ? -E_REND_TEST : REND_SUCCESS;
}

/* Test 2: Port cache behavior (LRU eviction, cache invalidation) */
static int smp_port_cache_test(u32 cpu_id)
{
        struct smp_port_test_state* state = &percpu(smp_port_test_state);
        memset(state, 0, sizeof(*state));
        
        if (!global_port_table) {
                pr_error("[smp_port_test] CPU %u: global_port_table is NULL\n", cpu_id);
                return -E_REND_TEST;
        }
        
        /* Phase 1: Fill cache with ports */
        for (u32 i = 0; i < SMP_PORT_MAX_PORTS_PER_CPU; i++) {
                make_port_name(state->port_names[i], SMP_PORT_NAME_LEN,
                              cpu_id + 1000, i); /* Use different base to avoid conflicts */
                
                Message_Port_t* port = create_message_port(state->port_names[i]);
                if (!port) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: create_message_port failed in cache test at i=%u\n",
                                        cpu_id, i);
                        }
                        continue;
                }
                
                error_t e = register_port(global_port_table, port);
                if (e != REND_SUCCESS) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: register_port failed in cache test at i=%u\n",
                                        cpu_id, i);
                        }
                        delete_message_port_structure(port);
                        continue;
                }
                
                /* Lookup to populate cache */
                Message_Port_t* found = thread_lookup_port(state->port_names[i]);
                if (!found) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: lookup failed in cache test at i=%u\n",
                                        cpu_id, i);
                        }
                        unregister_port(global_port_table, state->port_names[i]);
                        ref_put(&port->refcount, free_message_port_ref);
                        continue;
                }
                
                state->ports[i] = found;
                state->port_count++;
                state->register_count++;
                state->lookup_count++;
        }
        
        /* Phase 2: Repeatedly lookup cached ports (should hit cache) */
        for (u32 round = 0; round < SMP_PORT_TEST_ROUNDS; round++) {
                if (state->port_count == 0)
                        break;
                u32 idx = round % state->port_count;
                Message_Port_t* found = thread_lookup_port(state->port_names[idx]);
                if (!found || found != state->ports[idx]) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: cache lookup failed at round %u, idx=%u\n",
                                        cpu_id, round, idx);
                        }
                        continue;
                }
                state->lookup_count++;
                ref_put(&found->refcount, free_message_port_ref);
        }
        
        /* Phase 3: Unregister ports (should invalidate cache entries) */
        for (u32 i = 0; i < state->port_count; i++) {
                error_t e = unregister_port(global_port_table, state->port_names[i]);
                if (e != REND_SUCCESS) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: unregister failed in cache test at i=%u\n",
                                        cpu_id, i);
                        }
                } else {
                        state->unregister_count++;
                }
                
                /* Verify cache invalidation */
                Message_Port_t* found = thread_lookup_port(state->port_names[i]);
                if (found != NULL) {
                        state->error_count++;
                        if (state->error_count <= 5) {
                                pr_error("[smp_port_test] CPU %u: port still in cache after unregister at i=%u\n",
                                        cpu_id, i);
                        }
                        ref_put(&found->refcount, free_message_port_ref);
                }
                
                ref_put(&state->ports[i]->refcount, free_message_port_ref);
        }
        
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] CPU %u: Cache test done: reg=%x lookup=%x unreg=%x errors=%x\n",
                        cpu_id,
                        (u32)(state->register_count & 0xFFFFFFFF),
                        (u32)(state->lookup_count & 0xFFFFFFFF),
                        (u32)(state->unregister_count & 0xFFFFFFFF),
                        (u32)(state->error_count & 0xFFFFFFFF));
        }
        
        return (state->error_count > 0) ? -E_REND_TEST : REND_SUCCESS;
}

/* Test 3: Mixed operations (stress test) */
static int smp_port_mixed_test(u32 cpu_id)
{
        struct smp_port_test_state* state = &percpu(smp_port_test_state);
        memset(state, 0, sizeof(*state));
        
        if (!global_port_table) {
                pr_error("[smp_port_test] CPU %u: global_port_table is NULL\n", cpu_id);
                return -E_REND_TEST;
        }
        
        for (u32 round = 0; round < SMP_PORT_TEST_ROUNDS; round++) {
                u32 op = round % 4;
                
                switch (op) {
                case 0: { /* Create and register */
                        char port_name[SMP_PORT_NAME_LEN];
                        make_port_name(port_name, sizeof(port_name),
                                      cpu_id + 2000, round);
                        
                        Message_Port_t* port = create_message_port(port_name);
                        if (!port) {
                                state->error_count++;
                                if (state->error_count <= 5) {
                                        pr_error("[smp_port_test] CPU %u: create_message_port failed in mixed test at round %u\n",
                                                cpu_id, round);
                                }
                                break;
                        }
                        
                        error_t e = register_port(global_port_table, port);
                        if (e != REND_SUCCESS) {
                                state->error_count++;
                                if (state->error_count <= 5) {
                                        pr_error("[smp_port_test] CPU %u: register_port failed in mixed test at round %u\n",
                                                cpu_id, round);
                                }
                                delete_message_port_structure(port);
                                break;
                        }
                        state->register_count++;
                        
                        /* Store for later cleanup */
                        if (state->port_count < SMP_PORT_MAX_PORTS_PER_CPU) {
                                state->ports[state->port_count] = port;
                                my_strcpy(state->port_names[state->port_count], port_name);
                                state->port_count++;
                        } else {
                                /* Evict oldest */
                                unregister_port(global_port_table, state->port_names[0]);
                                ref_put(&state->ports[0]->refcount, free_message_port_ref);
                                /* Shift array */
                                for (u32 j = 0; j < state->port_count - 1; j++) {
                                        state->ports[j] = state->ports[j + 1];
                                        my_strcpy(state->port_names[j], state->port_names[j + 1]);
                                }
                                state->ports[state->port_count - 1] = port;
                                my_strcpy(state->port_names[state->port_count - 1], port_name);
                        }
                        break;
                }
                
                case 1: /* Lookup random port */
                case 2: {
                        if (state->port_count == 0)
                                break;
                        
                        u32 idx = round % state->port_count;
                        Message_Port_t* found = thread_lookup_port(state->port_names[idx]);
                        if (!found) {
                                state->error_count++;
                                if (state->error_count <= 5) {
                                        pr_error("[smp_port_test] CPU %u: lookup failed in mixed test at round %u, idx=%u\n",
                                                cpu_id, round, idx);
                                }
                                break;
                        }
                        state->lookup_count++;
                        ref_put(&found->refcount, free_message_port_ref);
                        break;
                }
                
                case 3: { /* Unregister random port */
                        if (state->port_count == 0)
                                break;
                        
                        u32 idx = round % state->port_count;
                        error_t e = unregister_port(global_port_table, state->port_names[idx]);
                        if (e != REND_SUCCESS) {
                                state->error_count++;
                                if (state->error_count <= 5) {
                                        pr_error("[smp_port_test] CPU %u: unregister failed in mixed test at round %u, idx=%u\n",
                                                cpu_id, round, idx);
                                }
                                break;
                        }
                        state->unregister_count++;
                        ref_put(&state->ports[idx]->refcount, free_message_port_ref);
                        
                        /* Remove from array */
                        for (u32 j = idx; j < state->port_count - 1; j++) {
                                state->ports[j] = state->ports[j + 1];
                                my_strcpy(state->port_names[j], state->port_names[j + 1]);
                        }
                        state->port_count--;
                        break;
                }
                }
                
                if (round % (SMP_PORT_TEST_ROUNDS / 10) == 0 && cpu_id == BSP_ID) {
                        pr_info("[smp_port_test] CPU %u: Mixed test round %u/%u\n",
                                cpu_id, round, SMP_PORT_TEST_ROUNDS);
                }
        }
        
        /* Cleanup remaining ports */
        for (u32 i = 0; i < state->port_count; i++) {
                unregister_port(global_port_table, state->port_names[i]);
                ref_put(&state->ports[i]->refcount, free_message_port_ref);
        }
        
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] CPU %u: Mixed test done: reg=%x lookup=%x unreg=%x errors=%x\n",
                        cpu_id,
                        (u32)(state->register_count & 0xFFFFFFFF),
                        (u32)(state->lookup_count & 0xFFFFFFFF),
                        (u32)(state->unregister_count & 0xFFFFFFFF),
                        (u32)(state->error_count & 0xFFFFFFFF));
        }
        
        return (state->error_count > 0) ? -E_REND_TEST : REND_SUCCESS;
}

int smp_port_robustness_test(void)
{
        u32 cpu_id = percpu(cpu_number);
        
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] Starting SMP port robustness test\n");
                if (!global_port_table) {
                        pr_error("[smp_port_test] global_port_table is NULL!\n");
                        return -E_REND_TEST;
                }
        }
        
        /* Test 1: Port table operations */
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] Test 1: Port table operations\n");
        }
        int ret1 = smp_port_table_test(cpu_id);
        if (ret1 != REND_SUCCESS) {
                return ret1;
        }
        
        /* Test 2: Port cache behavior */
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] Test 2: Port cache behavior\n");
        }
        int ret2 = smp_port_cache_test(cpu_id);
        if (ret2 != REND_SUCCESS) {
                return ret2;
        }
        
        /* Test 3: Mixed operations */
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] Test 3: Mixed operations\n");
        }
        int ret3 = smp_port_mixed_test(cpu_id);
        if (ret3 != REND_SUCCESS) {
                return ret3;
        }
        
        if (cpu_id == BSP_ID) {
                pr_info("[smp_port_test] All tests PASSED\n");
        }
        return REND_SUCCESS;
}
