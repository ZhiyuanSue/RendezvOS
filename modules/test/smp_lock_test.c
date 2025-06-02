#include <modules/test/test.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>

#define TEST_ROUND 100000

extern int NR_CPU;
spin_lock spin_ptr = NULL;
DEFINE_PER_CPU(struct spin_lock_t, test_spin_lock);
volatile int add_value = 0;
int smp_lock_test(void)
{
        struct spin_lock_t *my_spin_lock = &percpu(test_spin_lock);
        for (int i = 0; i < TEST_ROUND; i++) {
                lock_mcs(&spin_ptr, my_spin_lock);
                add_value++;
                unlock_mcs(&spin_ptr, my_spin_lock);
        }
        return 0;
}
int smp_lock_check(void)
{
        return NR_CPU * TEST_ROUND == add_value;
}
