#ifndef _RENDEZVOS_TASK_FP_H_
#define _RENDEZVOS_TASK_FP_H_
#include <common/types.h>
#include <common/string.h>
#include <common/align.h>
#include <rendezvos/error.h>

/*
 * Common FP buffer helpers.
 * Arch supplies: ARCH_FP_ALIGN, arch_fp_state_size, arch_fp_{cpu_init,payload_size,
 * save_to,load_from,init_area}.
 */

static inline u8* fp_area(void* buf)
{
        if (!buf)
                return NULL;
        return (u8*)ROUND_UP((uintptr_t)buf, ARCH_FP_ALIGN);
}

/** Copy FP area→area; no-op unless both have fp. */
static inline void fp_copy_ctx(Arch_Thread_Context* dst,
                               const Arch_Thread_Context* src)
{
        if (!dst || !src || !dst->fp || !src->fp)
                return;
        memcpy(fp_area(dst->fp), fp_area(src->fp), arch_fp_payload_size());
}

error_t fp_alloc(Arch_Thread_Context* ctx);
void fp_free(Arch_Thread_Context* ctx);

error_t fp_state_save(void* buf);
error_t fp_state_restore(void* buf);

#endif /* _RENDEZVOS_TASK_FP_H_ */
