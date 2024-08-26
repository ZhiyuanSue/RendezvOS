#ifndef _SHAMPOOS_X86_64_IO_H_
#define _SHAMPOOS_X86_64_IO_H_
#include <common/types.h>

static inline u_int8_t  inb(u_int16_t port) __attribute__((always_inline));
static inline u_int16_t inw(u_int16_t port) __attribute__((always_inline));
static inline u_int32_t inl(u_int16_t port) __attribute__((always_inline));

static inline void outb(u_int16_t port, u_int8_t data) __attribute__((always_inline));
static inline void outw(u_int16_t port, u_int16_t data) __attribute__((always_inline));
static inline void outl(u_int16_t port, u_int32_t data) __attribute__((always_inline));

inline u_int8_t inb(u_int16_t port) {
    u_int8_t data;

    __asm__ __volatile__("inb %1, %0" : "=a"(data) : "d"(port));
    return (data);
}
inline u_int16_t inw(u_int16_t port) {
    u_int16_t data;

    __asm__ __volatile__("inw %1, %0" : "=a"(data) : "d"(port));
    return (data);
}
inline u_int32_t inl(u_int16_t port) {
    u_int32_t data;

    __asm__ __volatile__("inl %1, %0" : "=a"(data) : "d"(port));
    return (data);
}

inline void outb(u_int16_t port, u_int8_t data) {
    __asm__ __volatile__("outb %0, %1" ::"a"(data), "d"(port));
}
inline void outw(u_int16_t port, u_int16_t data) {
    __asm__ __volatile__("outw %0, %1" ::"a"(data), "d"(port));
}
inline void outl(u_int16_t port, u_int32_t data) {
    __asm__ __volatile__("outl %0, %1" ::"a"(data), "d"(port));
}
#endif
