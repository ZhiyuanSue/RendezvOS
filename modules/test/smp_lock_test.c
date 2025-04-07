#include <modules/test/test.h>
#include <common/spin_lock.h>
#include <rendezvos/percpu.h>

DEFINE_PER_CPU(struct spin_lock_t, test_spin_lock);

int smp_lock_test(void)
{
        return 0;
}
