#include <arch/x86_64/PIC/APIC.h>
#include <shampoos/mm/map_handler.h>
#include <arch/x86_64/PIC/IRQ.h>
#include <common/types.h>
#include <shampoos/mm/vmm.h>
#include <modules/log/log.h>
extern struct map_handler Map_Handler;
extern int arch_irq_type;
void reset_xAPIC(void)
{
}
bool map_LAPIC(void)
{
        if (arch_irq_type != xAPIC_IRQ)
                return false; /*only xAPIC need mmio*/
        paddr vspace_root = get_current_kernel_vspace_root();
        paddr lapic_phy_page = xAPIC_MMIO_BASE;
        vaddr lapic_virt_page = KERNEL_PHY_TO_VIRT(xAPIC_MMIO_BASE);
        if (map(&vspace_root,
                PPN(lapic_phy_page),
                VPN(lapic_virt_page),
                3,
                PAGE_ENTRY_NONE,
                &Map_Handler)) {
                pr_error("[ LAPIC ] ERROR: map error\n");
                return false;
        }
        return true;
}
void lapic_get_vec(int bit, enum lapic_vec_type)
{
}
void lapic_set_vec(int bit, enum lapic_vec_type)
{
}
void lapci_clear_vec(int bit, enum lapic_vec_type)
{
}