#include <modules/pci/pci.h>
#include <modules/test/test.h>

#ifdef PCI
#include <modules/pci/pci_ops.h>
#include <common/string.h>
#include <rendezvos/mm/allocator.h>

static struct pci_node *simple_print_callback(u8 bus, u8 device, u8 func,
                                              const pci_header_t *hdr)
{
        pr_info("Found PCI device at %lx:%lx.%lx\n", bus, device, func);
        pr_info("  Vendor: %lx, Device: %lx\n",
                hdr->common.vendor_id,
                hdr->common.device_id);
        pr_info("  Class: %lx, Subclass: %lx, ProgIF: %lx\n",
                hdr->common.class_code,
                hdr->common.subclass,
                hdr->common.prog_if);
        if (hdr->common.vendor_id == 0x1022) { // AMD Vendor ID
                u16 device_id = hdr->common.device_id;

                pr_info("\nAMD Device at %lx:%lx.%lx\n", bus, device, func);

                // 显示设备类型
                const char *type = "Unknown";
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

                pr_info("  Device ID: %lx - %s\n", device_id, type);
                pr_info("  Class: %lx, Subclass: %lx\n",
                        hdr->common.class_code,
                        hdr->common.subclass);
        }
        if (hdr->common.vendor_id == 0x1002) { // AMD Vendor ID
                u16 device_id = hdr->common.device_id;

                pr_info("\nAMD Device at %lx:%lx.%lx\n", bus, device, func);
                const char *type = "Unknown";
                switch (device_id) {
                case 0x73A1:
                        type = "Radeon RX 7900 XTX";
                        break;
                case 0x73BF:
                        type = "Radeon RX 7900 XT";
                        break;
                case 0x1631:
                        type = "Radeon RX 6700 XT";
                        break;
                case 0x67DF:
                        type = "Radeon RX 470/480";
                        break;
                case 0x15D8:
                        type = "EPYC Integrated Graphics";
                        break;
                case 0x1480:
                        type = "Ryzen Integrated Graphics (Zen 3)";
                        break;
                default:
                        type = "AMD Graphics Card";
                }
                pr_info("  Device ID: %lx - %s\n", device_id, type);
                pr_info("  Class: %lx, Subclass: %lx\n",
                        hdr->common.class_code,
                        hdr->common.subclass);
        }

        struct allocator *malloc = kallocator;
        struct pci_node *pci_device_node =
                malloc->m_alloc(malloc, sizeof(struct pci_node));
        if (!pci_device_node) {
                pr_error("pci test: cannot alloc pci_node\n");
                return NULL;
        }
        memset(pci_device_node, '\0', sizeof(struct pci_node));
        pci_device_node->bus = bus;
        pci_device_node->device = device;
        pci_device_node->func = func;
        pci_device_node->vendor_id = hdr->common.vendor_id;
        pci_device_node->device_id = hdr->common.device_id;
        pci_device_node->class_code = hdr->common.class_code;
        pci_device_node->subclass = hdr->common.subclass;
        pci_device_node->prog_if = hdr->common.prog_if;

        pci_scan_bar(pci_device_node, hdr);
        return pci_device_node;
}
#endif

int test_pci_scan(void)
{
#ifdef PCI
        struct allocator *malloc = kallocator;
        struct pci_node *root =
                malloc->m_alloc(malloc, sizeof(struct pci_node));
        if (!root) {
                pr_error("pci test: cannot alloc root node\n");
                return -E_REND_TEST;
        }
        memset(root, '\0', sizeof(struct pci_node));
        return pci_scan_all(simple_print_callback, root);
#else
        return REND_SUCCESS;
#endif
}