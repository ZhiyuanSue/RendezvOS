#ifndef _RENDEZVOS_IO_APIC_H_
#define _RENDEZVOS_IO_APIC_H_

#include <common/types.h>
#include <common/stddef.h>
#include <common/stdbool.h>
#include <common/limits.h>
#include <rendezvos/error.h>

/* ACPI GSI; IOAPIC space uses global_irq_id == GSI (base 0). */
typedef u32 gsi_id_t;
#define GSI_ID_INVALID ((gsi_id_t)U32_MAX)

/* each RTE has 2 u32 registers (internal indices 0x10+2n / 0x11+2n) */
struct IOAPIC_RTE {
        /* First register; fields through Mask use bit 16 (17 bits total). */
        union {
                struct {
#define IOAPIC_RTE_VEC_OFF 0
                        u32 vector : 8;
#define IOAPIC_RTE_DELIVERY_MODE_OFF     8
#define IOAPIC_DELIVERY_MODE_FIXED       0u
#define IOAPIC_DELIVERY_MODE_LOWEST_PRIO 1u
#define IOAPIC_DELIVERY_MODE_SMI         2u
/* 3 reserved */
#define IOAPIC_DELIVERY_MODE_NMI  4u
#define IOAPIC_DELIVERY_MODE_INIT 5u
/* 6 reserved */
#define IOAPIC_DELIVERY_MODE_EXTINT 7u
                        u32 Delivery_Mode : 3;
#define IOAPIC_RTE_DEST_MODE_OFF 11
#define IOAPIC_DEST_PHYSICAL     0u
#define IOAPIC_DEST_LOGICAL      1u
                        u32 Dest_Mode : 1;
#define IOAPIC_RTE_DELIVERY_STATUS_OFF 12
#define IOAPIC_DELIVERY_STATUS_IDLE    0u
#define IOAPIC_DELIVERY_STATUS_PENDING 1u
                        u32 Delivery_Status : 1;
#define IOAPIC_RTE_PIN_POLARITY_OFF 13
#define IOAPIC_PIN_POLARITY_HIGH    0u
#define IOAPIC_PIN_POLARITY_LOW     1u
                        u32 Pin_Polarity : 1;
#define IOAPIC_RTE_REMOTE_IRR_OFF 14
#define IOAPIC_REMOTE_IRR_CLEAR   0u
#define IOAPIC_REMOTE_IRR_SET     1u
                        u32 Remote_IRR : 1;
#define IOAPIC_RTE_TRIGGER_MODE_OFF 15
#define IOAPIC_TRIGGER_MODE_EDGE    0u
#define IOAPIC_TRIGGER_MODE_LEVEL   1u
                        u32 Trigger_Mode : 1;
#define IOAPIC_RTE_MASK_OFF  16
#define IOAPIC_MASK_UNMASKED 0u
#define IOAPIC_MASK_MASKED   1u
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
/* +0x20 / +0x30: unused in the public MMIO window. */
/* Direct EOI register, only can be used when Version bits[7:0] >= 0x20.  */
#define IOAPIC_EOI 0x40

/* Internal register indices (write to IOREGSEL, then access IOWIN). */
#define IOAPIC_ID_NUM        0x0
#define IOAPIC_VERSION_NUM   0x1
#define IOAPIC_RTE_START_NUM 0x10
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
 * Bits 7:0 = IOAPIC version id:
 * - 0x11: classic 82093AA-class — no EOI MMIO at +0x40
 * - (0x12 .. 0x1F): not used in practice
 * - >= 0x20: directed EOI at MMIO +0x40 (RendezvOS's requirement)
 *
 * @c ioapic_init rejects any instance with version < @c
 * IOAPIC_VERSION_VALUE_LEGAL.
 */
#define IOAPIC_VERSION_VALUE_LEGAL 0x20u

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
        u8 version;
        u16 nr_pins;
        paddr mmio_phy_base;
        gsi_id_t gsi_base;
};

extern struct ioapic *ioapics;
extern size_t nr_ioapics;

size_t ioapic_nr_gsi(void);

error_t ioapic_get_entry_from_gsi(gsi_id_t gsi, struct ioapic **ioapic_entry_out,
                                  u16 *pin_out);

/**
 * @brief init ioapic data and structures.
 *
 * Requires @c ioapics[] from @c arch_parser_madt. After probe, registers the
 * IOAPIC irq_space (@c irq_space_ioapic_register).
 */
error_t ioapic_init(void);

/**
 * @brief Program the RTE for @p gsi; leave Mask=1.
 *
 * @p vector          IDT vector (after @c irq_global_alloc, usually
 *                    @c local_irq_id). Must be >= 16.
 * @p dest_apic_id    Destination field (physical: APIC ID; logical: mask).
 * @p delivery_mode   @c IOAPIC_DELIVERY_MODE_* (device path: Fixed).
 * @p dest_mode       @c IOAPIC_DEST_PHYSICAL / @c IOAPIC_DEST_LOGICAL.
 * @p trigger_mode    @c IOAPIC_TRIGGER_MODE_EDGE / @c
 * IOAPIC_TRIGGER_MODE_LEVEL.
 * @p pin_polarity    @c IOAPIC_PIN_POLARITY_HIGH / @c IOAPIC_PIN_POLARITY_LOW
 *                    (asserted level on the pin).
 *
 * Call @c ioapic_change_mask(@p gsi, false) when ready to accept the line.
 *
 * @return @c REND_SUCCESS, @c -E_IN_PARAM if input parameter is wrong, or @c
 * -E_REND_NOFOUND if the gsi is not found.
 */
error_t ioapic_configure_rte(gsi_id_t gsi, u8 vector, u8 dest_apic_id,
                             u32 delivery_mode, u32 dest_mode, u32 trigger_mode,
                             u32 pin_polarity);

/**
 * @brief Set or clear the RTE Mask bit for @p gsi.
 *
 * @p mask  true → Mask=1 (stop delivery); false → Mask=0 (allow delivery).
 */
error_t ioapic_change_mask(gsi_id_t gsi, bool mask);

/**
 * @brief Directed EOI: write @p vector to @p ioapic_entry's EOI register.
 *
 * Clears Remote IRR for level-triggered RTEs on this chip matching @p vector.
 * Harmless if Remote IRR is already clear. Caller resolves which IOAPIC
 * (e.g. via irq_space entry to @c ioapic); this API does not scan.
 */
void ioapic_eoi(struct ioapic *ioapic_entry, u8 vector);

#endif