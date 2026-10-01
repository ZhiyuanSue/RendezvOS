#ifndef _RENDEZVOS_SPIN_H_
#define _RENDEZVOS_SPIN_H_

/**
 * @brief Forever idle loop.
 */
static inline void cpu_idle(void)
{
        while (1)
                ;
}

#endif