#include <common/atomic.h>
#include <arch/x86_64/sync/barrier.h>
u64 atomic64_cas(volatile u64 *addr, u64 expected, u64 desired)
{
        u64 result;
        __asm__ __volatile__("lock; cmpxchgq %2, %1\n"
                             : "=a"(result), "+m"(*addr)
                             : "r"(desired), "0"(expected)
                             : "memory", "cc");
        return result;
}
u64 atomic64_exchange(volatile u64 *addr, u64 newval)
{
        u64 oldval;
        __asm__ __volatile__("lock xchgq %1, %0"
                             : "=r"(oldval), "+m"(*addr)
                             : "0"(newval)
                             : "memory");
        return oldval;
}

u64 atomic64_load(volatile const u64 *ptr)
{
        u64 value = *ptr;
        barrier();
        return value;
}
void atomic64_store(volatile u64 *ptr, u64 value)
{
        barrier();
        *ptr = value;
}
void atomic64_init(atomic64_t *ptr, i64 value)
{
        atomic64_store((volatile u64 *)&ptr->counter, (u64)value);
}
void atomic64_add(atomic64_t *ptr, i64 value)
{
        __asm__ volatile("lock addq %1, %0"
                         : "+m"(ptr->counter)
                         : "re"(value)
                         : "cc", "memory");
}
void atomic64_sub(atomic64_t *ptr, i64 value)
{
        __asm__ volatile("lock subq %1, %0"
                         : "+m"(ptr->counter)
                         : "re"(value)
                         : "cc", "memory");
}
i64 atomic64_fetch_add(atomic64_t *ptr, i64 value)
{
        i64 result;
        __asm__ volatile("lock xaddq %0, %1"
                         : "=r"(result), "+m"(ptr->counter)
                         : "0"(value)
                         : "cc", "memory");
        return result;
}
i64 atomic64_fetch_sub(atomic64_t *ptr, i64 value)
{
        return atomic64_fetch_add(ptr, -value);
}
void atomic64_inc(atomic64_t *ptr)
{
        __asm__ volatile("lock incq %0"
                         : "+m"(ptr->counter)
                         :
                         : "cc", "memory");
}
void atomic64_dec(atomic64_t *ptr)
{
        __asm__ volatile("lock decq %0"
                         : "+m"(ptr->counter)
                         :
                         : "cc", "memory");
}
i64 atomic64_fetch_inc(atomic64_t *ptr)
{
        i64 result = 1;
        __asm__ volatile("lock xaddq %0, %1"
                         : "+r"(result), "+m"(ptr->counter)
                         :
                         : "cc", "memory");
        return result;
}

i64 atomic64_fetch_dec(atomic64_t *ptr)
{
        i64 result = -1;
        __asm__ volatile("lock xaddq %0, %1"
                         : "+r"(result), "+m"(ptr->counter)
                         :
                         : "cc", "memory");
        return result;
}