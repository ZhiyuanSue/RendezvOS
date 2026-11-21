#include <common/rand.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/system/random.h>

/*
 * Per-CPU software state + optional HW fold. Software step is only
 * splitmix64() from common/rand.h.
 *
 * Call random_cpu_init() once per CPU after arch_enable_percpu +
 * arch_start_core (so percpu() and cpu_number are valid).
 */

DEFINE_PER_CPU(u64, random_state);

void random_cpu_init(void)
{
        u64 sample;

        /* Initial seed only; evolving counter lives in random_state. */
        u64 seed = splitmix64_seed_from((u64)percpu(cpu_number));
        if (arch_get_random_long(&sample))
                seed ^= sample;
        if (arch_get_random_long(&sample))
                seed ^= sample << 1;

        percpu(random_state) = seed;
}

u64 rand64(void)
{
        u64 sample;
        u64 *state;

        state = &percpu(random_state);

        if (arch_get_random_long(&sample)) {
                *state ^= sample;
                (void)splitmix64(state);
        }

        return splitmix64(state);
}
