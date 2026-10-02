#ifndef _RENDEZVOS_SMP_IPI_H_
#define _RENDEZVOS_SMP_IPI_H_

#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>

typedef u32 ipi_id_t;

#define IPI_ID_INVALID ((ipi_id_t)U32_MAX)

/**
 * @brief Soft-IPI callback called from IRQ context (no arguments).
 *
 * Payload / handshake state must live in caller-owned per-CPU storage filled
 * before @c smp_ipi_send. Handlers run in IRQ context — keep them short.
 */
typedef void (*smp_ipi_fn_t)(void);

/**
 * @brief Allocate a global soft-IPI slot and bind @p fn as callback.
 *
 * Slots are permanent (no unregister). Typically called once
 * during bring-up. At most @c RENDEZVOS_SMP_IPI_MAX slots.
 *
 * @param ipi_id_out Out: slot id in [0, @c RENDEZVOS_SMP_IPI_MAX)
 * @param fn Non-NULL handler
 * @return @c REND_SUCCESS , @c -E_IN_PARAM if @p ipi_id_out or @p fn is NULL,
 * or @c -E_REND_OVERFLOW if full
 */
error_t smp_ipi_register(ipi_id_t *ipi_id_out, smp_ipi_fn_t fn);

/**
 * @brief As we methioned , the ipi send is just a no arguments function.
 * So in order to send some informations, and let other CPU can handle it.
 * we must maintain some data and a state machine to discript how the ipi
 * producer and consumer communicate.
 * And that is what this function do, maintain the data and state machine
 * let another CPU can get the info , and then using arch_smp_ipi_send to send
 * it.
 *
 * @param cpu Destination that is @c cpu_is_online (x86: APIC id; aarch64:
 *        dense id ).
 * @param ipi_id Slot from @c smp_ipi_register
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if cpuid is wrong or cpu is disabled,
 * or arch error (@c -E_RENDEZVOS / …)
 */
error_t smp_ipi_send(cpu_id_t cpu, ipi_id_t ipi_id);

/**
 * @brief PerCPU: bind the soft-IPI IRQ vector with a handler.
 * for x86_64/aarch64, just a normal register_irq_handler.
 * but might be different for riscv64, so we must using this interface and
 * ignore the arch different
 */
void smp_ipi_init(void);

#endif
