#include "pci_dev_tree.h"
#include "pci.h"

typedef struct pci_node* (*pci_scan_callback)(u8 bus, u8 device, u8 func,
                                              const pci_header_t* hdr);
error_t pci_scan_bus(pci_scan_callback callback, u8 bus,
                     struct pci_node* parent_pci_tree_node);
error_t pci_scan_all(pci_scan_callback callback, struct pci_node* pci_root);
error_t pci_scan_bar(struct pci_node* pci_dev, const pci_header_t* hdr);

error_t pci_enable_device(struct pci_node* pci_device);
