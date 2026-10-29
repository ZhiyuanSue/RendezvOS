#ifndef _RENDEZVOS_IO_APIC_H_
#define _RENDEZVOS_IO_APIC_H_

#include <common/types.h>
#include <common/stddef.h>
#include <rendezvos/error.h>
#include <arch/x86_64/PIC/gsi.h>

/* each RTE has 2 u32 registers (internal indices 0x10+2n / 0x11+2n) */
struct IOAPIC_RTE {
        /* First register; fields through Mask use bit 16 (17 bits total). */
        union {
                struct {
#define IOAPIC_RTE_VEC_OFF 0
                        u32 vector : 8;
#define IOAPIC_RTE_DELIVERY_MODE_OFF 8
                        u32 Delivery_Mode : 3;
#define IOAPIC_RTE_DEST_MODE_OFF 11
                        u32 Dest_Mode : 1;
#define IOAPIC_RTE_DELIVERY_STATUS_OFF 12
                        u32 Delivery_Status : 1;
#define IOAPIC_RTE_PIN_POLARITY_OFF 13
                        u32 Pin_Polarity : 1;
#define IOAPIC_RTE_REMOTE_IRR_OFF 14
                        u32 Remote_IRR : 1;
#define IOAPIC_RTE_TRIGGER_MODE_OFF 15
                        u32 Trigger_Mode : 1;
#define IOAPIC_RTE_MASK_OFF 16
                        u32 Mask : 1;
                        u32 Reserve_0 : 15;
                };
                u32 reg_1;
        };
        /* Second register: Destination in bits 31:24 (RTE bits 63:56). */
        union {
                struct {
#define IOAPIC_RTE_DEST_OFF 24
                        u32 Reserve_1 : 24;
                        u32 Destination : 8;
                };
                u32 reg_2;
        };
};

/* MMIO window byte offsets (relative to mapped IOAPIC base VA). */
#define IOAPIC_IOREGSEL 0x00
#define IOAPIC_IOWIN    0x10

/* Internal register indices (write to IOREGSEL, then access IOWIN). */
#define IOAPIC_ID_NUM      0x0
#define IOAPIC_VERSION_NUM 0x1
/* RTE low/high for pin n: 0x10+2*n / 0x11+2*n */

/* IOAPIC ID register (index 0): APIC id in bits 27:24. */
#define IOAPIC_ID_OFF  24
#define IOAPIC_ID_MASK (0xf << IOAPIC_ID_OFF)

/*
 * Version register (index 1): MaxRTE in bits 23:16 (8-bit, 0..255).
 * Software stores nr_pins = MaxRTE + 1 (1..256), so @c struct ioapic::nr_pins
 * is @c u16 — @c u8 cannot hold 256.
 */
#define IOAPIC_VERSION_MAX_RTE_OFF  16
#define IOAPIC_VERSION_MAX_RTE_MASK (0xff << IOAPIC_VERSION_MAX_RTE_OFF)
#define IOAPIC_VERSION_MASK         0xff

/**
 * @brief Read internal register @p reg_index via IOREGSEL/IOWIN.
 *
 * @p vaddr_base is the identity VA of the IOAPIC MMIO window
 * (e.g. @c KERNEL_PHY_TO_VIRT(mmio_phy_base)), not a port number.
 */
#define IOAPIC_RD_REG(vaddr_base, reg_index)                          \
        ({                                                            \
                *((volatile u32 *)((vaddr_base) + IOAPIC_IOREGSEL)) = \
                        (u32)(reg_index);                             \
                *((volatile u32 *)((vaddr_base) + IOAPIC_IOWIN));     \
        })

/**
 * @brief Write internal register @p reg_index via IOREGSEL/IOWIN.
 */
#define IOAPIC_WR_REG(vaddr_base, reg_index, value)                   \
        do {                                                          \
                *((volatile u32 *)((vaddr_base) + IOAPIC_IOREGSEL)) = \
                        (u32)(reg_index);                             \
                *((volatile u32 *)((vaddr_base) + IOAPIC_IOWIN)) =    \
                        (u32)(value);                                 \
        } while (0)

/* Runtime IOAPIC descriptors (MADT discovery + Version probe). */
struct ioapic {
        u8 ioapic_id;
        u16 nr_pins;
        paddr mmio_phy_base;
        gsi_id_t gsi_base;
};

extern struct ioapic *ioapics;
extern size_t nr_ioapics;

/**
 * @brief init ioapic data and structures.
 *
 * Requires @c ioapics[] from @c arch_parser_madt.
 */
error_t ioapic_init(void);

#endif