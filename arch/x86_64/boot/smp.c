#include <arch/x86_64/smp.h>
#include <shampoos/time.h>
#include <modules/log/log.h>
#include <common/string.h>
extern char ap_start;
extern char ap_start_end;

static void copy_ap_start_code()
{
        char* dest_ap_start_ptr =
                (char*)KERNEL_PHY_TO_VIRT(_SHAMPOOS_X86_64_AP_PHY_ADDR_);
        char* src_ap_start_ptr = &ap_start;
        memcpy(dest_ap_start_ptr,
               src_ap_start_ptr,
               (vaddr)&ap_start_end - (vaddr)&ap_start);
}

void arch_start_smp(void)
{
        copy_ap_start_code();
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
                      PPN(_SHAMPOOS_X86_64_AP_PHY_ADDR_),
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      0);
        udelay(200);
        APIC_send_IPI(APIC_ICR_DEST_SH_ALL_EXCLUDE_SELF,
                      APIC_ICR_DEL_MODE_START_UP,
                      PPN(_SHAMPOOS_X86_64_AP_PHY_ADDR_),
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      0);
}