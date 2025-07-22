#include <modules/pci/pci.h>
int next_bus_number;
int recursion_depth;
/*TODO: enable device, alloc irq, configure bar*/
bool is_pci_bridge(pci_common_header_t *common)
{
        return common->class_code == 0x06;
}
bool pci_bridge_need_scan(pci_common_header_t *common)
{
        return common->class_code == 0x06
               && (common->subclass == 0x04 || common->subclass == 0x07);
}
void configure_pci_bridge_bus(u8 bus, u8 device, u8 func, u8 primary,
                              u8 secondary, u8 subordinate)
{
        pci_config_write_IO_dword(bus,
                                  device,
                                  func,
                                  0x18,
                                  ((subordinate << 16) | (secondary << 8)
                                   | (primary)));
}
struct pci_node *pci_scan_device(pci_scan_callback callback, u8 bus, u8 device,
                                 u8 func, pci_header_t *header,
                                 pci_common_header_t *common,
                                 struct pci_node *parent_pci_tree_node)
{
        u32 *ptr = (u32 *)common;
        struct pci_node *pci_device = NULL;
        error_t e = 0;
        for (int i = 0; i < 4; i++) {
                ptr[i] = pci_config_read_IO_dword(bus, device, 0, i * 4);
        }
        for (int i = 4; i < 16; i++) {
                ptr[i] = pci_config_read_IO_dword(bus, device, 0, i * 4);
        }
        if (pci_bridge_need_scan(common)) {
                u8 new_secondary = next_bus_number++;

                /*need to generate some data structure*/
                pci_device = callback(bus, device, 0, header);

                configure_pci_bridge_bus(
                        bus, device, func, bus, new_secondary, 0xFF);
                recursion_depth++;
                u8 bus_before = next_bus_number;
                e = pci_scan_bus(callback, new_secondary, pci_device);
                recursion_depth--;
                u8 new_subordinate = next_bus_number - 1;

                configure_pci_bridge_bus(
                        bus, device, func, bus, new_secondary, new_subordinate);
                if (e)
                        return NULL;

                /*TODO:we changed the pci bridge configure info, and we need to
                 * update it*/
                // pci_device-> xx = xx;

        } else {
                pci_device = callback(bus, device, 0, header);
        }
        return pci_device;
}
error_t pci_scan_bus(pci_scan_callback callback, u8 bus,
                     struct pci_node *parent_pci_tree_node)
{
        struct pci_node *pci_device = NULL;
        for (int device = 0; device < PCI_MAX_DEVICE; device++) {
                if (!pci_device_exists(bus, device, 0))
                        continue;
                pci_header_t header;
                pci_common_header_t *common = &header.common;

                pci_device = pci_scan_device(callback,
                                             bus,
                                             device,
                                             0,
                                             &header,
                                             common,
                                             parent_pci_tree_node);

                tree_node_insert(&parent_pci_tree_node->dev_node,
                                 &pci_device->dev_node);

                if (common->header_type & 0x80) {
                        for (int func = 1; func < PCI_MAX_FUNCTION; func++) {
                                if (pci_device_exists(bus, device, func)) {
                                        pci_device = pci_scan_device(
                                                callback,
                                                bus,
                                                device,
                                                func,
                                                &header,
                                                common,
                                                parent_pci_tree_node);
                                        tree_node_insert(
                                                &parent_pci_tree_node->dev_node,
                                                &pci_device->dev_node);
                                }
                        }
                }
        }
        return 0;
}
error_t pci_scan_all(pci_scan_callback callback, struct pci_node *pci_root)
{
        next_bus_number = 1;
        recursion_depth = 0;
        /*we only scan the bus 0 and other bus will be scanned from bus 0*/
        pci_scan_bus(callback, 0, pci_root);

        print("[    PCI start    ]\n");
        print_pci_tree(pci_root, 1);
        print("[    PCI end    ]\n");
        return 0;
}