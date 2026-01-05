#include <modules/test/test.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/sync/cas_lock.h>
#include <rendezvos/smp/percpu.h>

#define TEST_ROUND 100000

extern u32 BSP_ID;
extern int NR_CPU;
static spin_lock spin_ptr = NULL;
DEFINE_PER_CPU(struct spin_lock_t, test_spin_lock);
static cas_lock_t cas_lock;
static volatile int mcs_add_value = 0;
static volatile int cas_add_value = 0;
static volatile bool have_inited = false;
static atomic64_t atomic64_inc_value;
static atomic64_t atomic64_dec_value;
static atomic64_t atomic64_add_value;
static atomic64_t atomic64_sub_value;
int smp_lock_test(void)
{
        if (percpu(cpu_number) == BSP_ID) {
                lock_init_cas(&cas_lock);
                have_inited = true;
                atomic64_inc_value.counter = 0;
                atomic64_dec_value.counter = NR_CPU * TEST_ROUND;
                atomic64_add_value.counter = 0;
                atomic64_sub_value.counter = NR_CPU * TEST_ROUND * 2;
        } else {
                while (!have_inited)
                        ;
        }
        struct spin_lock_t *my_spin_lock = &percpu(test_spin_lock);
        for (int i = 0; i < TEST_ROUND; i++) {
                /*mcs spin lock*/
                lock_mcs(&spin_ptr, my_spin_lock);
                mcs_add_value++;
                unlock_mcs(&spin_ptr, my_spin_lock);
                /*cas spin lock*/
                lock_cas(&cas_lock);
                cas_add_value++;
                unlock_cas(&cas_lock);
        }
        /*atomic test*/
        for (int i = 0; i < TEST_ROUND; i++) {
                atomic64_inc(&atomic64_inc_value);
        }
        for (int i = 0; i < TEST_ROUND; i++) {
                atomic64_dec(&atomic64_dec_value);
        }
        for (int i = 0; i < TEST_ROUND; i++) {
                atomic64_add(&atomic64_add_value, 2);
        }
        for (int i = 0; i < TEST_ROUND; i++) {
                atomic64_sub(&atomic64_sub_value, 2);
        }
        return 0;
}
bool smp_lock_check(void)
{
        return NR_CPU * TEST_ROUND == mcs_add_value
               && NR_CPU * TEST_ROUND == cas_add_value
               && atomic64_inc_value.counter == NR_CPU * TEST_ROUND
               && atomic64_dec_value.counter == 0
               && atomic64_add_value.counter == NR_CPU * TEST_ROUND * 2
               && atomic64_sub_value.counter == 0;
}
