#include <modules/test/test.h>
#include <common/spin_lock.h>
#include <rendezvos/percpu.h>

#define TEST_ROUND 100000

int max_cpu_num = 0;
spin_lock spin_ptr = NULL;
DEFINE_PER_CPU(struct spin_lock_t, test_spin_lock);
int add_value = 0;
int smp_lock_test(void)
{
        if (percpu(cpu_number) > max_cpu_num)
                max_cpu_num = cpu_number;
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
        return (max_cpu_num + 1) * TEST_ROUND == add_value;
}
