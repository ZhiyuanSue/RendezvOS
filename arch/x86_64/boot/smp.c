#include <shampoos/smp.h>
#include <shampoos/time.h>
#include <modules/log/log.h>
#include <common/string.h>
extern char ap_start;
extern char ap_start_end;
extern int NR_CPU;
extern int BSP_ID;
extern enum cpu_status CPU_STATE;
extern struct nexus_node* nexus_root;
extern void clean_tmp_page_table();
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

void arch_start_smp(struct setup_info* arch_setup_info)
{
        if (NR_CPU > 1) {
                per_cpu(CPU_STATE, BSP_ID) = cpu_enable;
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
                                vaddr stack_top =
                                        (vaddr)get_free_page(2,
                                                             ZONE_NORMAL,
                                                             KERNEL_VIRT_OFFSET,
                                                             0,
                                                             per_cpu(nexus_root,
                                                                     BSP_ID))
                                        + 2 * PAGE_SIZE;
                                arch_setup_info->ap_boot_stack_ptr = stack_top;
                                arch_setup_info->cpu_id = i;
                                send_sipi(i, _SHAMPOOS_X86_64_AP_PHY_ADDR_);
                                for (int j = 0;
                                     j < 1000
                                     && per_cpu(CPU_STATE, i) == cpu_disable;
                                     j++) {
                                        mdelay(10);
                                }
                                if (per_cpu(CPU_STATE, i) == cpu_disable) {
                                        pr_error(
                                                "[ ERROR ]cpu %d cannot enable after 10s\n",
                                                i);
                                }
                        }
                }
                /* if all ap is enabled, then we should clean ap start
                 * code*/
                clean_ap_start_code();
        }
        /*if no ap is used, we also need to clean the low address of tmp
         * page table*/
        clean_tmp_page_table();
}