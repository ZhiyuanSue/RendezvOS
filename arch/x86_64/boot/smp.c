#include <arch/x86_64/smp.h>
#include <shampoos/time.h>
void arch_start_smp(void)
{
        APIC_send_IPI(APIC_ICR_DEST_SH_ALL_EXCLUDE_SELF,
                      APIC_ICR_DEL_MODE_INIT,
                      0,
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      0);
        mdelay(10);
        APIC_send_IPI(APIC_ICR_DEST_SH_ALL_EXCLUDE_SELF,
                      APIC_ICR_DEL_MODE_START_UP,
                      0,
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      0);
}