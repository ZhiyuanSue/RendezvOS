#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/PIC/LocalAPIC.h>
#include <arch/x86_64/cpuid.h>
#include <arch/x86_64/msr.h>
#include <arch/x86_64/sys_ctrl.h>
#include <modules/log/log.h>
#include <common/bit.h>
#include <modules/acpi/acpi_madt.h>

extern struct cpuinfo_x86 cpuinfo;
int arch_irq_type = NO_IRQ;
static inline bool xAPIC_support(void)
{
        return (cpuinfo.feature_2 & X86_CPUID_FEATURE_EDX_APIC);
}
static inline bool x2APIC_support(void)
{
        return (cpuinfo.feature_1 & X86_CPUID_FEATURE_ECX_x2APIC);
}
static inline void enable_xAPIC(void)
{
        u64 APIC_BASE_val;

        APIC_BASE_val = rdmsr(IA32_APIC_BASE_addr);
        APIC_BASE_val = set_mask(APIC_BASE_val, IA32_APIC_BASE_X_ENABLE);
        wrmsr(IA32_APIC_BASE_addr, APIC_BASE_val);
}
static inline void disable_xAPIC(void)
{
        u64 APIC_BASE_val;

        APIC_BASE_val = rdmsr(IA32_APIC_BASE_addr);
        APIC_BASE_val = clear_mask(APIC_BASE_val, IA32_APIC_BASE_X_ENABLE);
        wrmsr(IA32_APIC_BASE_addr, APIC_BASE_val);
}
static inline bool xapic_check_base_addr(void)
{
        extern struct acpi_table_madt *madt_table;
        if (madt_table->Local_int_ctrl_address != xAPIC_MMIO_BASE) {
                pr_error(
                        "[ERROR] using xAPIC but address is not match that in acpi table\n");
                return false;
        }
        return true;
}
void init_irq(void)
{
        if (xAPIC_support()) {
                if (x2APIC_support()) {
                        pr_info("support and use x2APIC\n");
                        arch_irq_type = x2APIC_IRQ;
                        disable_PIC();
                        // TODO: x2APIC
                        // seems the same like the xAPIC
                        // but no need to map the apic, and the address search
                        // is not the same
                } else {
                        pr_info("no x2APIC support and we use the Local xAPIC\n");
                        arch_irq_type = xAPIC_IRQ;
                        disable_PIC();
                        if (!xapic_check_base_addr())
                                return;
                        if (!map_LAPIC()) {
                                return;
                        }
                        reset_xAPIC();
                        enable_xAPIC();
                }
        } else {
                pr_info("use 8259A\n");
                arch_irq_type = PIC_IRQ;
                init_PIC();
        }
}