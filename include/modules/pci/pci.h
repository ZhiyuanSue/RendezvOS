#ifndef _RENDEZVOS_PCI_H_
#define _RENDEZVOS_PCI_H_
#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>
#include <common/stdbool.h>
#include <modules/log/log.h>

/* pci address reg's macros */
#define PCI_ADDR_REG_ENABLE      (((u32)1) << 31)
#define PCI_ADDR_REG_BUS_OFF     (16)
#define PCI_ADDR_REG_BUS_MASK    (0xFF)
#define PCI_ADDR_REG_DEVICE_OFF  (11)
#define PCI_ADDR_REG_DEVICE_MASK (0x1F)
#define PCI_ADDR_REG_FUNC_OFF    (8)
#define PCI_ADDR_REG_FUNC_MASK   (0x7)
#define PCI_ADDR_REG_OFF_MASK    (0xFC)

#define PCI_MAX_BUS      256
#define PCI_MAX_DEVICE   32
#define PCI_MAX_FUNCTION 8

#define SET_PCI_ADDR_REG_VAL(bus, device, func, offset)                     \
        (PCI_ADDR_REG_ENABLE                                                \
         | ((bus & PCI_ADDR_REG_BUS_MASK) << PCI_ADDR_REG_BUS_OFF)          \
         | ((device & PCI_ADDR_REG_DEVICE_MASK) << PCI_ADDR_REG_DEVICE_OFF) \
         | ((func & PCI_ADDR_REG_FUNC_MASK) << PCI_ADDR_REG_FUNC_OFF)       \
         | (offset & PCI_ADDR_REG_OFF_MASK))
/* rd/wr port way */
static inline u32 pci_config_read_IO_dword(u8 bus, u8 device, u8 function,
                                           u8 offset)
{
        u32 address = SET_PCI_ADDR_REG_VAL(bus, device, function, offset);

        outl(_X86_PCI_ADDR_REG, address);
        return inl(_X86_PCI_DATA_REG);
}
static inline void pci_config_write_IO_dword(u8 bus, u8 device, u8 function,
                                             u8 offset, u32 value)
{
        u32 address = SET_PCI_ADDR_REG_VAL(bus, device, function, offset);

        outl(_X86_PCI_ADDR_REG, address);
        outl(_X86_PCI_DATA_REG, value);
}
static inline bool pci_device_exists(u8 bus, u8 device, u8 func)
{
        u32 vid = pci_config_read_IO_dword(bus, device, func, 0x00);
        return (vid != 0xFFFFFFFF);
}
/*MMIO way*/

/*pci header*/

typedef struct {
        u16 vendor_id;
        u16 device_id;
        u16 command;
        u16 status;
        u8 revision_id;
        u8 prog_if;
        u8 subclass;
        u8 class_code;
        u8 cache_line_size;
        u8 latency_timer;
        u8 header_type;
        u8 bist;
} pci_common_header_t;

// Type 0: 标准设备头部
typedef struct {
        pci_common_header_t common;

        u32 bar[6];
        u32 cardbus_cis;
        u16 subsystem_vendor_id;
        u16 subsystem_id;
        u32 expansion_rom_addr;
        u8 capabilities_ptr;
        u8 reserved[7];
        u8 interrupt_line;
        u8 interrupt_pin;
        u8 min_grant;
        u8 max_latency;
} pci_header_type0_t;

// Type 1
typedef struct {
        pci_common_header_t common;

        u32 bar[2];
        u8 primary_bus;
        u8 secondary_bus;
        u8 subordinate_bus;
        u8 secondary_latency;
        u8 io_base;
        u8 io_limit;
        u16 secondary_status;
        u16 memory_base;
        u16 memory_limit;
        u16 prefetch_memory_base;
        u16 prefetch_memory_limit;
        u32 prefetch_base_upper;
        u32 prefetch_limit_upper;
        u16 io_base_upper;
        u16 io_limit_upper;
        u8 capabilities_ptr;
        u8 reserved[3];
        u32 expansion_rom_addr;
        u8 interrupt_line;
        u8 interrupt_pin;
        u16 bridge_control;
} pci_header_type1_t;

typedef union {
        pci_common_header_t common;
        pci_header_type0_t type0;
        pci_header_type1_t type1;
} pci_header_t;

/*for operation system data structure */
typedef struct pci_dev pci_dev_t;

typedef struct pci_bus pci_bus_t;

struct pci_bus {};

struct pci_dev {};

typedef error_t (*pci_scan_callback)(u8 bus, u8 device, u8 func,
                                     const pci_header_t* hdr);
error_t pci_scan_bus(pci_scan_callback callback, u8 bus);
error_t pci_scan_all(pci_scan_callback callback);

#endif