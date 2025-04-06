#ifndef _RENDEZVOS_SPIN_H_
#define _RENDEZVOS_SPIN_H_

static inline void cpu_idle(void)
{
        while (1)
                ;
}

#endif