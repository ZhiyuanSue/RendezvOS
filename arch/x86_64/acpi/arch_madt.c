#include <common/stddef.h>
#include <arch/x86_64/acpi/arch_madt.h>
#include <modules/log/log.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>

struct acpi_table_madt *madt_table;
extern int NR_CPU;

error_t arch_parser_madt(void)
{
        NR_CPU = 0;
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++) {
                /*clean the state*/
                per_cpu(CPU_STATE, i) = no_cpu;
        }
        for_each_madt_ctrl_head(madt_table)
        {
                print("curr ctrl head type %d\n", curr_ctrl_head->type);
                switch (curr_ctrl_head->type) {
                case madt_ctrl_type_Local_APIC: {
                        struct madt_Local_APIC *lapic =
                                (struct madt_Local_APIC *)curr_ctrl_head;
                        cpu_id_t cpu_apic_id = lapic->_APIC_ID;
                        if (!(lapic->flags & Local_APIC_flags_enable)) {
                                print("Local APIC id %d disabled in MADT, skip\n",
                                      cpu_apic_id);
                                break;
                        }
                        print("Local APIC id is %d\n", cpu_apic_id);
                        if (cpu_apic_id < RENDEZVOS_MAX_CPU_NUMBER) {
                                NR_CPU++;
                                per_cpu(CPU_STATE, cpu_apic_id) = cpu_disable;
                        }
                        break;
                }
                case madt_ctrl_type_IO_APIC:
                        break;
                case madt_ctrl_type_Source_Override:
                        break;
                default:
                        break;
                }
        }
        return REND_SUCCESS;
}
