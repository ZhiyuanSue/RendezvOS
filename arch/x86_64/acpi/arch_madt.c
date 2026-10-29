#include <common/stddef.h>
#include <arch/x86_64/acpi/arch_madt.h>
#include <arch/x86_64/PIC/IOAPIC.h>
#include <modules/log/log.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/error.h>

struct acpi_table_madt *madt_table;
extern int NR_CPU;

static void ioapics_sort_by_gsi_base(void)
{
        for (size_t i = 1; i < nr_ioapics; i++) {
                struct ioapic key = ioapics[i];
                size_t j = i;

                while (j > 0 && ioapics[j - 1].gsi_base > key.gsi_base) {
                        ioapics[j] = ioapics[j - 1];
                        j--;
                }
                ioapics[j] = key;
        }
}

error_t arch_parser_madt(void)
{
        struct allocator *cpu_kallocator;
        struct ioapic *ioapic_entry;

        NR_CPU = 0;
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++)
                per_cpu(CPU_STATE, i) = no_cpu;

        /* Pass 1: Local APIC topology + count IOAPIC entries. */
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
                        nr_ioapics++;
                        break;
                case madt_ctrl_type_Source_Override:
                        break;
                default:
                        break;
                }
        }

        /* ACPI allows zero IOAPIC */
        if (nr_ioapics) {
                cpu_kallocator = per_cpu(kallocator, BSP_ID);
                if (!cpu_kallocator || !cpu_kallocator->m_alloc) {
                        print("[ MADT ] ERROR: no kallocator for ioapics\n");
                        nr_ioapics = 0;
                        return -E_RENDEZVOS;
                }
                ioapics = cpu_kallocator->m_alloc(
                        cpu_kallocator, sizeof(struct ioapic) * nr_ioapics);
                if (!ioapics) {
                        print("[ MADT ] ERROR: ioapics alloc failed\n");
                        nr_ioapics = 0;
                        return -E_RENDEZVOS;
                }
        } else {
                print("[ MADT ] no IOAPIC entries\n");
        }

        /* Pass 2: fill IOAPIC descriptors; other types (ISO, …) later. */
        ioapic_entry = ioapics;
        for_each_madt_ctrl_head(madt_table)
        {
                switch (curr_ctrl_head->type) {
                case madt_ctrl_type_IO_APIC: {
                        struct madt_IO_APIC *madt_io =
                                (struct madt_IO_APIC *)curr_ctrl_head;

                        ioapic_entry->ioapic_id = madt_io->IO_APIC_ID;
                        ioapic_entry->mmio_phy_base =
                                (paddr)madt_io->IO_APIC_Addr;
                        ioapic_entry->gsi_base =
                                (gsi_id_t)madt_io->Global_Sys_Int_Base;
                        ioapic_entry++;
                        break;
                }
                case madt_ctrl_type_Source_Override:
                        break;
                default:
                        break;
                }
        }

        if (nr_ioapics)
                ioapics_sort_by_gsi_base();
        return REND_SUCCESS;
}
