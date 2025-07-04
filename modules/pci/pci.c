#include <modules/pci/pci.h>

error_t pci_scan_device(pci_scan_callback callback, u8 bus, u8 device, u8 func,
                        pci_header_t *header, pci_common_header_t *common)
{
        u32 *ptr = (u32 *)common;
        error_t e = 0;
        for (int i = 0; i < 4; i++) {
                ptr[i] = pci_config_read_IO_dword(bus, device, 0, i * 4);
        }
        u8 header_type = common->header_type & 0x7F;

        switch (header_type) {
        case 0x00:
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                e = callback(bus, device, 0, header);
                break;

        case 0x01:
                /*
                        TODO : we need to add the bridge bus id allocation,
                        to avoid infinite recursion
                        beside, we need to write into the pci space
                */
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                e = callback(bus, device, 0, header);

                if (e)
                        return e;

                e = pci_scan_bus(callback, header->type1.secondary_bus);
                break;

        case 0x02:
                for (int i = 4; i < 16; i++) {
                        ptr[i] =
                                pci_config_read_IO_dword(bus, device, 0, i * 4);
                }
                e = callback(bus, device, 0, header);
                break;

        default:
                break;
        }
        return e;
}
error_t pci_scan_bus(pci_scan_callback callback, u8 bus)
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
        return 0;
}
error_t pci_scan_all(pci_scan_callback callback)
{
        for (int bus = 0; bus < PCI_MAX_BUS; bus++) {
                pci_scan_bus(callback, bus);
        }
        return 0;
}