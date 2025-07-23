#ifndef _RENDEZVOS_PCI_TREE_H_
#define _RENDEZVOS_PCI_TREE_H_

#include <common/types.h>
#include <common/dsa/tree.h>
#define MAX_PCI_BAR_NUMBER 6

struct pci_resource {
        u64 start_addr;
        u64 len;
/*the flags might be different with the Linux, please be careful*/
#define PCI_RESOURCE_EXIST    0x1
#define PCI_RESOURCE_IO       0x2
#define PCI_RESOURCE_MEM      0x4
#define PCI_RESOURCE_MEM_64   0x8
#define PCI_RESOURCE_PREFETCH 0x10
        u64 flags;
};
struct pci_node {
        struct tree_node dev_node;
        /*id info*/
        u8 bus;
        u8 device;
        u8 func;
        u8 pedding;
        /*header info*/
        u16 vendor_id;
        u16 device_id;
        u8 prog_if;
        u8 subclass;
        u8 class_code;
        u8 header_type;
        /*resources*/
        u32 usable_resource_number;
        struct pci_resource bar[MAX_PCI_BAR_NUMBER];

        /*bus info*/
        u8 primary;
        u8 secondary;
        u8 subordinate;
        u8 padding;
        /*device state*/
        u32 pci_device_flags;
        u32 ref_count;
};
extern struct pci_node* pci_root;

error_t pci_enable_device(struct pci_node* pci_device);
void print_pci_tree(struct pci_node* pci_parent_node, int level);
struct pci_node* pci_get_device(u16 vendor_id, u16 device_id,
                                struct pci_node* start_node);
void pci_put_device(struct pci_node* pci_device);
static inline void pci_tree_set_pci_bus_info(struct pci_node* pci_device,
                                             u8 primary, u8 secondary,
                                             u8 subordinate)
{
        pci_device->primary = primary;
        pci_device->secondary = secondary;
        pci_device->subordinate = subordinate;
}
static inline u64 pci_resource_start(struct pci_node* pci_device, int bar_id)
{
        return pci_device->bar[bar_id].start_addr;
}
static inline u64 pci_resource_len(struct pci_node* pci_device, int bar_id)
{
        return pci_device->bar[bar_id].len;
}

#endif