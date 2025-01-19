#include <arch/x86_64/smp.h>
#include <shampoos/time.h>
#include <modules/log/log.h>
#include <common/string.h>
extern char ap_start;
extern char ap_start_end;
extern int NR_CPU;
extern enum cpu_status CPU_STATE;
static void copy_ap_start_code()
{
        char* dest_ap_start_ptr =
                (char*)KERNEL_PHY_TO_VIRT(_SHAMPOOS_X86_64_AP_PHY_ADDR_);
        char* src_ap_start_ptr = &ap_start;
        memcpy(dest_ap_start_ptr,
               src_ap_start_ptr,
               (vaddr)&ap_start_end - (vaddr)&ap_start);
}
static void clean_ap_start_code()
{
        /*clean the ap start code for avoid atteck*/
        char* dest_ap_start_ptr =
                (char*)KERNEL_PHY_TO_VIRT(_SHAMPOOS_X86_64_AP_PHY_ADDR_);
        memset(dest_ap_start_ptr, 0x0, (vaddr)&ap_start_end - (vaddr)&ap_start);
}
static void send_sipi(int cpu_id, paddr ap_start_addr)
{
        APIC_send_IPI(cpu_id,
                      APIC_ICR_DEST_SH_NO,
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      APIC_ICR_DEL_MODE_START_UP,
                      PPN(ap_start_addr));
        udelay(200);
        APIC_send_IPI(cpu_id,
                      APIC_ICR_DEST_SH_NO,
                      0,
                      APIC_ICR_LEVEL,
                      0,
                      APIC_ICR_DEL_MODE_START_UP,
                      PPN(ap_start_addr));
}

void arch_start_smp(void)
{
        if (NR_CPU > 1) {
                per_cpu(CPU_STATE, 0) = cpu_enable;
                /*
                    if we only have one cpu,no need for this,
                    we only have one single cpu and we needn't mark that this
                   cpu is on
                */
                copy_ap_start_code();

                /*send init ipi to all cores*/
                APIC_send_IPI(0,
                              APIC_ICR_DEST_SH_ALL_EXCLUDE_SELF,
                              0,
                              APIC_ICR_LEVEL,
                              0,
                              APIC_ICR_DEL_MODE_INIT,
                              0);
                mdelay(10);

                for (int i = 0; i < SHAMPOOS_MAX_CPU_NUMBER; i++) {
                        if (per_cpu(CPU_STATE, i) == cpu_disable) {
                                send_sipi(i, _SHAMPOOS_X86_64_AP_PHY_ADDR_);
                        }
                        /*TODO: ap should fill in the cpu_enable, and bsp should
                         * wait*/
                }

                clean_ap_start_code();
        }
}