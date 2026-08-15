#ifndef _RENDEZVOS_SMP_IPI_H_
#define _RENDEZVOS_SMP_IPI_H_

#include <rendezvos/error.h>
#include <rendezvos/limits.h>
#include <rendezvos/smp/percpu.h>

typedef u32 ipi_id_t;

#define IPI_ID_INVALID ((ipi_id_t)U32_MAX)

typedef void (*smp_ipi_fn_t)(void);

error_t smp_ipi_register(ipi_id_t *ipi_id_out, smp_ipi_fn_t fn);
error_t smp_ipi_send(cpu_id_t cpu, ipi_id_t ipi_id);
void smp_ipi_init(void);

#endif
