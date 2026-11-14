#include <rendezvos/task/thread.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/task/fp.h>

/*
 * Shared FP buffer lifecycle + sole HW↔SIZE+ALIGN snapshot path.
 * Arch only supplies: arch_fp_save_to / load_from / init_area (+ SIZE/ALIGN).
 * Allocator zero-fills; fp_alloc only applies arch defaults via init_area.
 */

error_t fp_alloc(Arch_Thread_Context* ctx)
{
        if (!ctx)
                return -E_IN_PARAM;
        if (ctx->fp)
                return REND_SUCCESS;

        struct allocator* cpu_kallocator = percpu(kallocator);

        ctx->fp = (u8*)cpu_kallocator->m_alloc(cpu_kallocator, ARCH_FP_BUF_SIZE);
        if (!ctx->fp)
                return -E_REND_NO_MEM;

        arch_fp_init_area(fp_area(ctx->fp));
        return REND_SUCCESS;
}

void fp_free(Arch_Thread_Context* ctx)
{
        if (!ctx || !ctx->fp)
                return;
        struct allocator* cpu_kallocator = percpu(kallocator);
        cpu_kallocator->m_free(cpu_kallocator, ctx->fp);
        ctx->fp = NULL;
}

size_t fp_state_size(void)
{
        return ARCH_FP_BUF_SIZE;
}

error_t fp_state_save(void* buf)
{
        if (!buf)
                return -E_IN_PARAM;
        arch_fp_save_to(fp_area(buf));
        return REND_SUCCESS;
}

error_t fp_state_restore(void* buf)
{
        if (!buf)
                return -E_IN_PARAM;
        arch_fp_load_from(fp_area(buf));
        return REND_SUCCESS;
}
