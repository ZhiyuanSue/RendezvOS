#ifndef _RENDEZVOS_PCI_H_
#define _RENDEZVOS_PCI_H_
#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>
#include <common/stdbool.h>

/* pci address reg's macros */
#define PCI_ADDR_REG_ENABLE      (((u32)1) << 31)
#define PCI_ADDR_REG_BUS_OFF     (16)
#define PCI_ADDR_REG_BUS_MASK    (0xFF)
#define PCI_ADDR_REG_DEVICE_OFF  (11)
#define PCI_ADDR_REG_DEVICE_MASK (0x1F)
#define PCI_ADDR_REG_FUNC_OFF    (8)
#define PCI_ADDR_REG_FUNC_MASK   (0x7)
#define PCI_ADDR_REG_OFF_MASK    (0xFC)

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
        return (vid != 0xFFFF);
}
/*MMIO way*/
#endif