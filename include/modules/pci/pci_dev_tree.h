#ifndef _RENDEZVOS_PCI_TREE_H_
#define _RENDEZVOS_PCI_TREE_H_

#include <common/types.h>
#include <common/dsa/tree.h>
#define MAX_PCI_BAR_NUMBER 6

struct pci_resource {
        u64 start_addr;
        u64 len;
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

        /*device state*/
        u32 pci_device_flags;
};
extern struct pci_node* pci_root;

error_t pci_enable_device(struct pci_node* pci_device);
void print_pci_tree(struct pci_node* pci_parent_node, int level);

#endif