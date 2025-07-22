#ifndef _RENDEZVOS_PCI_TREE_H_
#define _RENDEZVOS_PCI_TREE_H_

#include <common/types.h>
#include <common/dsa/tree.h>

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
};
extern struct pci_node* pci_root;

error_t pci_enable_device(struct pci_node* pci_device);
void print_pci_tree(struct pci_node* pci_parent_node, int level);

#endif