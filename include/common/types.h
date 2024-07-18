/*
 * This file is used to define all kinds of basic types used in ShamPoOS, as we
 * use no std
 */
#ifndef _SHAMPOOS_TYPES_H_
#define _SHAMPOOS_TYPES_H_

typedef signed char i8;
typedef unsigned char u8;

typedef signed short i16;
typedef unsigned short u16;

typedef signed int i32;
typedef unsigned int u32;

typedef signed long long int i64;
typedef unsigned long long int u64;

/*
 *  The following definations are defined in stdint.h, I just repeated it.
 */
typedef i8 int8_t;
typedef u8 u_int8_t;
typedef i16 int16_t;
typedef u16 u_int16_t;
typedef i32 int32_t;
typedef u32 u_int32_t;
typedef i64 int64_t;
typedef u64 u_int64_t;

typedef u_int8_t uint8_t;
typedef u_int16_t uint16_t;
typedef u_int32_t uint32_t;
typedef u_int64_t uint64_t;

typedef unsigned long uintptr_t;

typedef u64 addr_t;
typedef addr_t vaddr;
typedef addr_t paddr;
typedef int error_t;

#endif