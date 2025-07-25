#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <common/bit.h>
#include <modules/log/log.h>
#include <modules/acpi/acpi_madt.h>

enum IRQ_type arch_irq_type = NO_IRQ;
static inline bool xapic_check_base_addr(void)
{
        extern struct acpi_table_madt *madt_table;
        if (madt_table->Local_int_ctrl_address != xAPIC_MMIO_BASE) {
                print("[ERROR] using xAPIC but address is not match that in acpi table\n");
                return false;
        }
        return true;
}
void init_irq(void)
{
        if (xAPIC_support()) {
                if (x2APIC_support()) {
                        print("support and use x2APIC\n");
                        arch_irq_type = x2APIC_IRQ;
                        disable_PIC();
                        // TODO: x2APIC
                        // seems the same like the xAPIC
                        // but no need to map the apic, and the address search
                        // is not the same
                        enable_x2APIC();
                        reset_APIC();
                } else {
                        print("no x2APIC support and we use the Local xAPIC\n");
                        arch_irq_type = xAPIC_IRQ;
                        disable_PIC();
                        if (!xapic_check_base_addr())
                                return;
                        if (!map_LAPIC()) {
                                return;
                        }
                        enable_xAPIC();
                        reset_xAPIC_LDR();
                        reset_APIC();
                }
        } else {
                print("use 8259A\n");
                arch_irq_type = PIC_IRQ;
                init_PIC();
        }
}