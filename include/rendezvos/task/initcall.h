#ifndef _RENDEZVOS_INIT_H_
#define _RENDEZVOS_INIT_H_
#include <common/types.h>

typedef struct {
        void (*init_func_ptr)(void);
} Init_Info;
extern Init_Info _s_init, _e_init;

#define __INIT_SECTION(level) ".init.call." #level
#define DEFINE_INIT_LEVEL(func_ptr, level)                              \
        __attribute__((used, section(__INIT_SECTION(level)))) Init_Info \
                __init_##func_ptr = {.init_func_ptr = (func_ptr)}
#define DEFINE_INIT(func_ptr) DEFINE_INIT_LEVEL(func_ptr, 3)

static inline void do_init_call(void)
{
        for (Init_Info *i_ptr = &_s_init; i_ptr < &_e_init; i_ptr++) {
                i_ptr->init_func_ptr();
        }
}
#endif