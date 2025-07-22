#include <modules/pci/pci_tree.h>
#include <modules/log/log.h>

struct pci_node* pci_root;

void print_pci_tree(struct pci_node* pci_parent_node, int level)
{
        struct tree_node* parent_t_node = &pci_parent_node->dev_node;
        for_each_tree_node_child(parent_t_node)
        {
                struct pci_node* pci_device =
                        container_of(t_node, struct pci_node, dev_node);
                for (int i = 0; i < level; i++)
                        print("  ");
                print("Found PCI device at %x:%x.%x\n",
                      pci_device->bus,
                      pci_device->device,
                      pci_device->func);
                for (int i = 0; i < level; i++)
                        print("  ");
                print("  Vendor: %x, Device: %x\n",
                      pci_device->vendor_id,
                      pci_device->device_id);
                for (int i = 0; i < level; i++)
                        print("  ");
                print("  Class: %x, Subclass: %x, ProgIF: %x\n",
                      pci_device->class_code,
                      pci_device->subclass,
                      pci_device->prog_if);
                print_pci_tree(pci_device, level + 1);
        }
}
