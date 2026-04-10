#ifndef _RENDEZVOS_BITMAP_H_
#define _RENDEZVOS_BITMAP_H_

#include <common/align.h>
#include <common/bit.h>
#include <common/stdbool.h>
#include <common/stddef.h>
#include <common/types.h>

/*
 * Fixed-cell bitmap (u64 cells).
 *
 * BITMAP_DEFINE_TYPE(Name, nb_bits)
 *   struct Name { cells[] } + Name##_zero / Name##_is_zero / Name##_test /
 *   Name##_set / Name##_clear.
 *
 * BITMAP_OPS(prefix, verb)
 *   Expands to prefix##_t##_##verb, i.e. the operation on type (prefix##_t).
 *   Convention: typedef struct Name ... Name must be literally prefix + "_t".
 *   Example: Name = asid_bitmap_t, prefix = asid_bitmap →
 *   BITMAP_OPS(asid_bitmap, zero)(&obj)  →  asid_bitmap_t_zero(&obj).
 *
 * nb_bits must be a compile-time constant.
 */
#define BITMAP_CELL_BITS 64u
#define BITMAP_CELL_TYPE u64

#define BITMAP_NUM_CELLS(nb_bits) \
        (ROUND_UP((nb_bits), BITMAP_CELL_BITS) / BITMAP_CELL_BITS)

#define BITMAP_DEFINE_TYPE(Name, nb_bits)                               \
        typedef struct Name {                                           \
                BITMAP_CELL_TYPE cells[BITMAP_NUM_CELLS(nb_bits)];      \
        } Name;                                                         \
        static inline void Name##_zero(Name *b)                         \
        {                                                               \
                size_t _n = BITMAP_NUM_CELLS(nb_bits);                  \
                for (size_t i = 0; i < _n; i++)                         \
                        b->cells[i] = 0;                                \
        }                                                               \
        static inline bool Name##_is_zero(const Name *b)                \
        {                                                               \
                size_t _n = BITMAP_NUM_CELLS(nb_bits);                  \
                for (size_t i = 0; i < _n; i++) {                       \
                        if (b->cells[i] != 0)                           \
                                return false;                           \
                }                                                       \
                return true;                                            \
        }                                                               \
        static inline bool Name##_test(const Name *b, u32 bit)          \
        {                                                               \
                u32 _cell = bit / BITMAP_CELL_BITS;                     \
                u32 _sub = bit % BITMAP_CELL_BITS;                      \
                return (b->cells[_cell] & BIT_U64(_sub)) != 0;          \
        }                                                               \
        static inline void Name##_set(Name *b, u32 bit)                 \
        {                                                               \
                u32 _cell = bit / BITMAP_CELL_BITS;                     \
                u32 _sub = bit % BITMAP_CELL_BITS;                      \
                b->cells[_cell] =                                       \
                        set_mask_u64(b->cells[_cell], BIT_U64(_sub));   \
        }                                                               \
        static inline void Name##_clear(Name *b, u32 bit)               \
        {                                                               \
                u32 _cell = bit / BITMAP_CELL_BITS;                     \
                u32 _sub = bit % BITMAP_CELL_BITS;                      \
                b->cells[_cell] =                                       \
                        clear_mask_u64(b->cells[_cell], BIT_U64(_sub)); \
        }

#define BITMAP_OPS(prefix, verb) prefix##_t##_##verb

#endif