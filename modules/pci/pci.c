#include <modules/pci/pci.h>

void pci_scan_device(pci_scan_callback callback, u8 bus, u8 device, u8 func,
                     pci_header_t *header, pci_common_header_t *common)
{
        uint32_t *ptr = (uint32_t *)common;
        for (int i = 0; i < 4; i++) {
                ptr[i] = pci_config_read_IO_dword(bus, device, 0, i * 4);
        }
        uint8_t header_type = common->header_type & 0x7F;

        switch (header_type) {
        case 0x00:
                pr_info("type 0 device\n");
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                callback(bus, device, 0, header);
                break;

        case 0x01:
				/*
					TODO : we need to add the bridge bus id allocation,
					to avoid infinite recursion
					beside, we need to write into the pci space
				*/
                pr_info("type 1 device\n");
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                callback(bus, device, 0, header);

                pr_info("PCI-PCI Bridge %x:%x.%x: Primary=%d Secondary=%d Subordinate=%d\n",
                        bus,
                        device,
                        0,
                        header->type1.primary_bus,
                        header->type1.secondary_bus,
                        header->type1.subordinate_bus);

                pci_scan_bus(callback, header->type1.secondary_bus);
                break;

        case 0x02:
                pr_info("type 2 device\n");
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                callback(bus, device, 0, header);
                break;

        default:
                pr_info("Unknown header type %x at%x:%x.%x\n",
                        header_type,
                        bus,
                        device,
                        0);
                break;
        }
}
void pci_scan_bus(pci_scan_callback callback, u8 bus)
{
        for (int device = 0; device < PCI_MAX_DEVICE; device++) {
                if (!pci_device_exists(bus, device, 0))
                        continue;
                pci_header_t header;
                pci_common_header_t *common = &header.common;

                pci_scan_device(callback, bus, device, 0, &header, common);

                if (common->header_type & 0x80) {
                        for (int func = 1; func < PCI_MAX_FUNCTION; func++) {
                                if (pci_device_exists(bus, device, func)) {
                                        pci_scan_device(callback,
                                                        bus,
                                                        device,
                                                        func,
                                                        &header,
                                                        common);
                                }
                        }
                }
        }
}
void pci_scan_all(pci_scan_callback callback)
{
        for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
                pci_scan_bus(callback, bus);
        }
}