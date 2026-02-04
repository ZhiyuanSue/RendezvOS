#ifndef _RENDEZVOS_INIT_H_
#define _RENDEZVOS_INIT_H_
#include <common/types.h>

typedef struct {
        void (*init_func_ptr)(void);
} Init_Info;
extern Init_Info _s_init, _e_init;

#define INIT_SECTION ".init.call"
#define DEFINE_INIT(func_ptr)                                  \
        __attribute__((used, section(INIT_SECTION))) Init_Info \
                __init_##func_ptr = {.init_func_ptr = (func_ptr)}

static inline void do_init_call(void)
{
        for (Init_Info *i_ptr = &_s_init; i_ptr < &_e_init; i_ptr++) {
                i_ptr->init_func_ptr();
        }
}
#endif