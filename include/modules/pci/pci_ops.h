#ifndef _RENDEZVOS_PCI_OPS_H_
#define _RENDEZVOS_PCI_OPS_H_
#include "pci_dev_tree.h"
#include "pci.h"

/**
 * @brief Per-device callback during scan
 * You can allocate/fill @c pci_node , or do other things (or NULL, do nothing).
 * Bridge devices may use the returned node as parent for the secondary bus.
 */
typedef struct pci_node* (*pci_scan_callback)(u8 bus, u8 device, u8 func,
                                              const pci_header_t* hdr);

/**
 * @brief Scan one bus
 */
error_t pci_scan_bus(pci_scan_callback callback, u8 bus,
                     struct pci_node* parent_pci_tree_node);
/**
 * @brief Scan from bus 0 under @p pci_root.
 */
error_t pci_scan_all(pci_scan_callback callback, struct pci_node* pci_root);
/**
 * @brief Probe each BAR size and record resources.
 * Reuses already-programmed BAR addresses; does not allocate new ones.
 */
error_t pci_scan_bar(struct pci_node* pci_dev, const pci_header_t* hdr);

/**
 * @brief Enable IO / MEM / bus master on the device and assign IRQ if needed.
 *
 * actually it's just a placeholder
 * @return May fail when IRQ assignment is unavailable.
 */
error_t pci_enable_device(struct pci_node* pci_device);
#endif
