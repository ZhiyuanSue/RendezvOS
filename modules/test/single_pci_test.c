#include <modules/pci/pci.h>
#include <modules/test/test.h>

error_t simple_print_callback(u8 bus, u8 device, u8 func,
                              const pci_header_t* hdr)
{
        pr_info("Found PCI device at %x:%x.%x\n", bus, device, func);
        pr_info("  Vendor: %x, Device: %x\n",
                hdr->common.vendor_id,
                hdr->common.device_id);
        pr_info("  Class: %x, Subclass: %x, ProgIF: %x\n",
                hdr->common.class_code,
                hdr->common.subclass,
                hdr->common.prog_if);
        if (hdr->common.vendor_id == 0x1022) { // AMD Vendor ID
                uint16_t device_id = hdr->common.device_id;

                pr_info("\nAMD Device at %x:%x.%x\n", bus, device, func);

                // 显示设备类型
                const char* type = "Unknown";
                if (device_id >= 0x1000 && device_id <= 0x10FF)
                        type = "AMD Processor";
                else if (device_id >= 0x6700 && device_id <= 0x67FF)
                        type = "Radeon 600 Series";
                else if (device_id >= 0x6800 && device_id <= 0x69FF)
                        type = "Radeon 7000 Series";
                else if (device_id >= 0x7300 && device_id <= 0x73FF)
                        type = "Radeon RX 7000 Series";
                else if (device_id >= 0x7900 && device_id <= 0x79FF)
                        type = "Chipset SATA Controller";

                pr_info("  Device ID: %x - %s\n", device_id, type);
                pr_info("  Class: %x, Subclass: %x\n",
                        hdr->common.class_code,
                        hdr->common.subclass);
        }
        if (hdr->common.vendor_id == 0x1002) { // AMD Vendor ID
                uint16_t device_id = hdr->common.device_id;

                pr_info("\nAMD Device at %x:%x.%x\n", bus, device, func);
                const char* type = "Unknown";
                switch (device_id) {
                case 0x73A1:
                        type = "Radeon RX 7900 XTX";
                case 0x73BF:
                        type = "Radeon RX 7900 XT";
                case 0x1631:
                        type = "Radeon RX 6700 XT";
                case 0x67DF:
                        type = "Radeon RX 470/480";
                case 0x15D8:
                        type = "EPYC Integrated Graphics";
                case 0x1480:
                        type = "Ryzen Integrated Graphics (Zen 3)";
                default:
                        type = "AMD Graphics Card";
                }
                pr_info("  Device ID: %x - %s\n", device_id, type);
                pr_info("  Class: %x, Subclass: %x\n",
                        hdr->common.class_code,
                        hdr->common.subclass);
        }
        return 0;
}

int test_pci_scan(void)
{
#ifdef PCI
        return pci_scan_all(simple_print_callback);
#else
        return 0;
#endif
}