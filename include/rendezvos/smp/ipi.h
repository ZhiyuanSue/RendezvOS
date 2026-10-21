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
 * @brief Allocate a global soft-IPI slot and install @p fn as callback.
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
 * @brief Send a soft-IPI to @p cpu for slot @p ipi_id.
 *
 * Soft-IPI carries no payload in the send path. Shared state and any
 * producer/consumer handshake live in caller-owned storage filled before
 * this call; the arch send only signals the remote CPU.
 *
 * @param cpu_id Destination that is @c cpu_is_online
 * @param ipi_id Slot from @c smp_ipi_register
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if cpu_id is wrong or cpu is offline,
 * or arch error (@c -E_RENDEZVOS / …)
 */
error_t smp_ipi_send(cpu_id_t cpu_id, ipi_id_t ipi_id);

/**
 * @brief Per-CPU bring-up: register the soft-IPI local_irq_id handler.
 *
 * Arch implements @c arch_smp_ipi_init; callers use this so the IRQ id and
 * EOI policy stay behind the arch hook.
 */
void smp_ipi_init(void);

#endif
