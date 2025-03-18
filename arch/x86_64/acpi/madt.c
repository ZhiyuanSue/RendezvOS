#include <modules/acpi/acpi.h>
#include <modules/log/log.h>
#include <rendezvos/smp.h>
#include <common/stddef.h>
#include <rendezvos/percpu.h>
struct acpi_table_madt *madt_table;
extern int NR_CPU;
extern enum cpu_status CPU_STATE;
error_t parser_apic()
{
        NR_CPU = 0;
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                /*clean the state*/
                per_cpu(CPU_STATE, i) = no_cpu;
        }
        for_each_madt_ctrl_head(madt_table)
        {
                pr_info("curr ctrl head type %d\n", curr_ctrl_head->type);
                switch (curr_ctrl_head->type) {
                case madt_ctrl_type_Local_APIC:
                        int cpu_apic_id =
                                ((struct madt_Local_APIC *)curr_ctrl_head)
                                        ->_APIC_ID;
                        pr_info("Local APIC id is %d\n", cpu_apic_id);
                        if (cpu_apic_id < RENDEZVOS_MAX_CPU_NUMBER) {
                                NR_CPU++;
                                per_cpu(CPU_STATE, cpu_apic_id) = cpu_disable;
                        }
                        break;
                case madt_ctrl_type_IO_APIC:
                        break;
                case madt_ctrl_type_Source_Override:
                        break;
                default:
                        break;
                }
        }
        return 0;
}