#include <modules/pci/pci_dev_tree.h>
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

                for (int i = 0; i < MAX_PCI_BAR_NUMBER; i++) {
                        if (pci_device->bar[i].flags & PCI_RESOURCE_EXIST) {
                                for (int j = 0; j < level; j++)
                                        print("  ");
                                print("  BAR%d start: 0x%x, len: 0x%x, flags:0x%x\n",
                                      i,
                                      pci_device->bar[i].start_addr,
                                      pci_device->bar[i].len,
                                      pci_device->bar[i].flags);
                        }
                }
                print_pci_tree(pci_device, level + 1);
        }
}
struct pci_node* pci_get_device_dfs(u16 vendor_id, u16 device_id,
                                    struct pci_node* start_node)
{
        if (vendor_id == start_node->vendor_id
            && device_id == start_node->device_id) {
                return start_node;
        }
        struct tree_node* parent_t_node = &start_node->dev_node;
        for_each_tree_node_child(parent_t_node)
        {
                struct pci_node* res = pci_get_device_dfs(
                        vendor_id,
                        device_id,
                        container_of(t_node, struct pci_node, dev_node));
                if (res)
                        return res;
        }
        return NULL;
}
struct pci_node* pci_get_device(u16 vendor_id, u16 device_id,
                                struct pci_node* start_node)
{
        if (!start_node)
                start_node = pci_root;
        struct pci_node* res =
                pci_get_device_dfs(vendor_id, device_id, start_node);
        if (res)
                res->ref_count += 1;
        return res;
}
void pci_put_device(struct pci_node* pci_device)
{
        pci_device->ref_count -= 1;
        if (pci_device->ref_count < 0)
                pci_device->ref_count = 0;
}