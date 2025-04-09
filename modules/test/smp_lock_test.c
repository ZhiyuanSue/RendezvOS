#include <modules/test/test.h>
#include <common/spin_lock.h>
#include <rendezvos/percpu.h>

spin_lock spin_ptr = NULL;
DEFINE_PER_CPU(struct spin_lock_t, test_spin_lock);

int smp_lock_test(void)
{
        struct spin_lock_t *my_spin_lock = &percpu(test_spin_lock);
        for (int i = 0; i < 100; i++) {
                lock_mcs(&spin_ptr, my_spin_lock);
                pr_info("this is my spinlock %d\n", i);
                unlock_mcs(&spin_ptr, my_spin_lock);
        }
        return 0;
}
