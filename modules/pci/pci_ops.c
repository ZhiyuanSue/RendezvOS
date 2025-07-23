#include <modules/pci/pci_ops.h>
#include <common/string.h>
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
error_t pci_scan_bar(struct pci_node *pci_dev, const pci_header_t *hdr)
{
        u64 bar_offset = 0;
        u64 last_bar_offset = 0;
        if (hdr->common.header_type == 0) {
                bar_offset = (vaddr) & (hdr->type0.bar[0]) - (vaddr)hdr;
                last_bar_offset = (vaddr) & (hdr->type0.bar[5]) - (vaddr)hdr;
        } else if (hdr->common.header_type == 1) {
                bar_offset = (vaddr) & (hdr->type1.bar[0]) - (vaddr)hdr;
                last_bar_offset = (vaddr) & (hdr->type0.bar[1]) - (vaddr)hdr;
        }
        memset(pci_dev->bar,
               '\0',
               sizeof(struct pci_resource) * MAX_PCI_BAR_NUMBER);
        for (u64 offset = bar_offset; offset <= last_bar_offset; offset += 4) {
                int bar_number = (offset - bar_offset) / 4;
                u32 origin_val, probe_val;
                u32 size = 0;
                origin_val = pci_config_read_IO_dword(
                        pci_dev->bus, pci_dev->device, pci_dev->func, offset);

                if (!origin_val)
                        continue;
                pci_dev->bar[bar_number].flags |= PCI_RESOURCE_EXIST;

                pci_config_write_IO_dword(pci_dev->bus,
                                          pci_dev->device,
                                          pci_dev->func,
                                          offset,
                                          PCI_BASE_ADDRESS_PROBE_MASK);
                probe_val = pci_config_read_IO_dword(
                        pci_dev->bus, pci_dev->device, pci_dev->func, offset);
                pci_config_write_IO_dword(pci_dev->bus,
                                          pci_dev->device,
                                          pci_dev->func,
                                          offset,
                                          origin_val);

                if (probe_val & PCI_BASE_ADDRESS_SPACE_IO) {
                        pci_dev->bar[bar_number].flags |= PCI_RESOURCE_IO;
                        size = probe_val & PCI_BASE_ADDRESS_IO_MASK;
                } else {
                        pci_dev->bar[bar_number].flags |= PCI_RESOURCE_MEM;
                        if (probe_val & PCI_BASE_ADDRESS_MEM_TYPE_PREFETCH) {
                                pci_dev->bar[bar_number].flags |=
                                        PCI_RESOURCE_PREFETCH;
                        }
                        if ((probe_val & PCI_BASE_ADDRESS_MEM_TYPE_MASK)
                            == PCI_BASE_ADDRESS_MEM_TYPE_64) {
                                pci_dev->bar[bar_number].flags |=
                                        PCI_RESOURCE_MEM_64;
                        }
                        size = probe_val & PCI_BASE_ADDRESS_MEM_MASK;
                }
                size = ~size + 1;
                pci_dev->bar[bar_number].len = size;
                pci_dev->bar[bar_number].start_addr = origin_val;

                if (pci_dev->bar[bar_number].flags & PCI_RESOURCE_MEM_64) {
                        offset += 4;
                        if (offset > last_bar_offset) {
                                pr_error(
                                        "[ERROR] PCI find a 64 BAR but out of range\n");
                                return -1;
                        }
                        u32 origin_val_hi, probe_val_hi;
                        u32 size_hi;

                        origin_val_hi =
                                pci_config_read_IO_dword(pci_dev->bus,
                                                         pci_dev->device,
                                                         pci_dev->func,
                                                         offset);

                        pci_config_write_IO_dword(pci_dev->bus,
                                                  pci_dev->device,
                                                  pci_dev->func,
                                                  offset,
                                                  PCI_BASE_ADDRESS_PROBE_MASK);
                        probe_val_hi = pci_config_read_IO_dword(pci_dev->bus,
                                                                pci_dev->device,
                                                                pci_dev->func,
                                                                offset);
                        pci_config_write_IO_dword(pci_dev->bus,
                                                  pci_dev->device,
                                                  pci_dev->func,
                                                  offset,
                                                  origin_val_hi);
                        size_hi = ~probe_val_hi + 1;
                        pci_dev->bar[bar_number].len = ((u64)size_hi << 32)
                                                       | size;
                        pci_dev->bar[bar_number].start_addr =
                                (((u64)origin_val_hi) << 32) | origin_val;
                }
                /*TODO:ROM dev*/
        }
        return 0;
}