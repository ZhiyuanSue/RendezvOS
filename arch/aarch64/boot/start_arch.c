#include <arch/aarch64/boot/arch_setup.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <arch/aarch64/cpuinfo.h>
#include <arch/aarch64/gic/gic.h>
#include <common/endianness.h>
#include <common/mm.h>
#include <modules/dtb/dtb.h>
#include <modules/dtb/print_property.h>
#include <modules/log/log.h>
#include <arch/aarch64/psci/psci.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/cpu_topology.h>
#include <rendezvos/smp/ipi.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/smp/smp.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/allocator.h>
#include <rendezvos/trap/trap.h>
#include <rendezvos/system/syscall.h>
#include <rendezvos/time.h>
#include <arch/aarch64/sys_ctrl.h>
#include <arch/aarch64/sync/barrier.h>

extern u64 L2_table;
extern char *cmdline_ptr;
extern int NR_CPU;
cpu_id_t BSP_ID = 0;
struct cpuinfo cpu_info = {0};
DEFINE_PER_CPU(struct device_node*, cpu_device_node);

/*
 * Merge user PSTATE I (from SPSR at trap entry) into live DAIF.
 * D/A/F stay as the current kernel policy (typically masked after exception
 * entry). Aligns with x86 syscall entry sti intent: syscall window respects
 * user IF when set.
 */
static void arch_inherit_daif_i_from_user(u64 spsr)
{
        u64 live;

        mrs("DAIF", live);
        live = (live & ~PSTATE_DAIF_I) | (spsr & PSTATE_DAIF_I);
        msr("DAIF", live);
        isb();
}

/*
 * TRAP_CLASS_SYSCALL fixed handler: arch entry glue, then portable syscall().
 * EL0 SVC only: save live DAIF, inherit user I for the syscall window, restore
 * on return (same pattern as x86 sti/cli around the syscall body).
 * Same-EL SVC (EC 0x18) keeps kernel DAIF policy unchanged.
 */
static void arch_syscall_helper(struct trap_info *info)
{
        struct trap_frame *tf = info ? info->tf : NULL;
        u64 saved_daif = 0;
        bool from_user = info && info->is_user;

        if (from_user) {
                mrs("DAIF", saved_daif);
                arch_inherit_daif_i_from_user(tf->SPSR);
        }
        syscall(tf);
        if (from_user) {
                msr("DAIF", saved_daif);
                isb();
        }
}

/*
 * Bind syscall via trap_class; arch_register_fixed_trap() reverse-scans
 * aarch64_ec_to_trap_class() (EC 0x15 -> TRAP_CLASS_SYSCALL).
 */
static void init_syscall(void)
{
        arch_register_fixed_trap(
                TRAP_CLASS_SYSCALL, arch_syscall_helper, IRQ_NO_ATTR);
}
/*
 * @brief get_cpu_info read some cpu register and read the cpu infomation to the
 * structure cpu_info
 */
static void get_cpu_info(void)
{
        mrs("MPIDR_EL1", cpu_info.mpidr_el1_val);
}
/*
 * @brief mapping the aarch64 dtb
 * @param arch_setup_info the setup info structure, which include the dtb
 * physical address
 */
static void map_dtb(struct setup_info *arch_setup_info)
{
        vaddr vaddr;
        paddr paddr;
        u64 offset;
        ARCH_PFLAGS_t flags;

        /*
         * map the dtb, using the linux boot protocol, which define that:
         * the dtb must be 8 byte align, and less then 2m
         * as it haven't defined that dtb must 2m align

         * we have memcpy at boot map stage,
         * and we can sure that now the dtb data is 2m align,
         * so we just need to map only one 2m ,not 4m now
        */
        vaddr = ROUND_UP(arch_setup_info->map_end_virt_addr, MIDDLE_PAGE_SIZE);
        paddr = ROUND_DOWN(arch_setup_info->dtb_ptr, MIDDLE_PAGE_SIZE);
        flags = arch_decode_flags(2,
                                  PAGE_ENTRY_GLOBAL | PAGE_ENTRY_HUGE
                                          | PAGE_ENTRY_READ | PAGE_ENTRY_VALID);
        arch_set_L2_entry(paddr, vaddr, (union L2_entry *)&L2_table, flags);
        offset = vaddr - paddr;
        arch_setup_info->boot_dtb_header_base_addr =
                arch_setup_info->dtb_ptr + offset;
        arch_setup_info->map_end_virt_addr =
                arch_setup_info->boot_dtb_header_base_addr + MIDDLE_PAGE_SIZE;
}
/*
 * @brief here just map and check the dtb
 * @param arch_setup_info the setup info structure, which include the dtb
 * physical address
 */
error_t prepare_arch(struct setup_info *arch_setup_info)
{
        map_dtb(arch_setup_info);
        struct fdt_header *dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);
        if (fdt_check_header(dtb_header_ptr)) {
                print("check fdt header fail\n");
                goto prepare_arch_error;
        }
        // parse_print_dtb(dtb_header_ptr, 0, 0);

        return (0);
prepare_arch_error:
        return (-E_RENDEZVOS);
}
/*
 * @brief use get_cpu_info to get cpu infomation and set current cpu as bsp
 * @param arch_setup_info the setup info structure, which include the dtb
 * physical address
 */
error_t arch_cpu_info(struct setup_info *arch_setup_info)
{
        /*read MPIDR to get the cpu affinity*/
        (void)arch_setup_info;
        get_cpu_info();
        cpu_hw_map_reset();
        set_cpu_hw_id(BSP_ID, (cpu_hwid_t)MPIDR_HWID(cpu_info.mpidr_el1_val));
        return REND_SUCCESS;
}
/*
 * @brief recursively build the device tree structure from dtb file
 * @param malloc the allocator of each device node space
 * @param parent the parent node, used for tree build
 * @param fdt the dtb ptr
 * @param offset the offset from dtb
 * @param depth the tree node depth
 */
struct device_node *build_device_tree(struct allocator *malloc,
                                      struct device_node *parent, void *fdt,
                                      int offset, int depth)
{
        char *ch;
        struct fdt_property *prop;
        const char *property_name;

        struct device_node *curr_node = NULL;

        struct property *curr_property = NULL;
        struct property *head_property = NULL;

        int property, node;
        ch = (char *)fdt + fdt_off_dt_struct(fdt) + offset + FDT_TAGSIZE;

        curr_node = malloc->m_alloc(malloc, sizeof(struct device_node));
        curr_node->name = ch;
        curr_node->property = NULL;

        if (parent) {
                tree_node_insert(&parent->dev_node, &curr_node->dev_node);
        } else {
                tree_node_insert(NULL, &curr_node->dev_node);
        }

        fdt_for_each_property_offset(property, fdt, offset)
        {
                prop = (struct fdt_property *)fdt_offset_ptr(
                        fdt, property, FDT_TAGSIZE);
                property_name = (char *)fdt_string(
                        fdt, SWAP_ENDIANNESS_32(prop->nameoff));

                enum property_type_enum p_type =
                        get_property_type(property_name);
                if (p_type) {
                        curr_property = malloc->m_alloc(
                                malloc, sizeof(struct property));

                        curr_property->name = (char *)property_name;
                        curr_property->data = prop->data;
                        curr_property->len = SWAP_ENDIANNESS_32(prop->len);

                        if (!curr_node->property) {
                                curr_node->property = curr_property;
                        } else {
                                head_property->next = curr_property;
                        }
                        head_property = curr_property;
                }
        }

        fdt_for_each_subnode(node, fdt, offset)
        {
                build_device_tree(malloc, curr_node, fdt, node, depth + 1);
        }
        return curr_node;
}
/* This function enumerate the cpus from DTB */
static void arch_enumerate_cpus(void)
{
        for (int i = 0; i < RENDEZVOS_MAX_CPU_NUMBER; i++)
                per_cpu(CPU_STATE, i) = no_cpu;
        per_cpu(CPU_STATE, BSP_ID) = cpu_disable;
        NR_CPU = 1;

#ifdef NR_CPUS
        if (NR_CPUS == 1)
                return;
#endif
        struct device_node *cpu_node = dev_node_find_by_type(NULL, "cpu");
        while (cpu_node) {
                struct property *reg_prop =
                        dev_node_find_property(cpu_node, "reg", 4);
                if (!reg_prop) {
                        pr_error(
                                "[ SMP ] cannot find reg property in cpu node\n");
                        goto next_cpu_node;
                }
                struct property *method_prop =
                        dev_node_find_property(cpu_node, "enable-method", 14);
                if (!method_prop) {
                        pr_error("[ SMP ] cpu node missing enable-method\n");
                        goto next_cpu_node;
                }
                char *method_str;
                property_read_string(method_prop, &method_str);
                if (strcmp_s(method_str, "psci", 5)) {
                        pr_error(
                                "[ SMP ] we only support psci smp setup method now\n");
                        goto next_cpu_node;
                }
                u32 reg_val;
                if (property_read_u32(reg_prop, &reg_val))
                        goto next_cpu_node;

                cpu_hwid_t hwid = (cpu_hwid_t)reg_val;
                if (hwid == cpu_hw_id(BSP_ID)) {
                        per_cpu(cpu_device_node, BSP_ID) = cpu_node;
                        goto next_cpu_node;
                }
                if (NR_CPU >= RENDEZVOS_MAX_CPU_NUMBER)
                        goto next_cpu_node;
#ifdef NR_CPUS
                if (NR_CPU >= NR_CPUS)
                        goto next_cpu_node;
#endif
                set_cpu_hw_id((cpu_id_t)NR_CPU, hwid);
                per_cpu(CPU_STATE, NR_CPU) = cpu_disable;
                per_cpu(cpu_device_node, NR_CPU) = cpu_node;
                NR_CPU++;
        next_cpu_node:
                cpu_node = dev_node_find_by_type(dev_tree_get_next(cpu_node),
                                                 "cpu");
        }
}
/*
 * @brief this function is after the vmm start, so we can use kallocator now,
 in this function, we need to handle all of the platform part, e.g. aarch64 dtb
 part, and the gic part
 * @param arch_setup_info the setup info structure, which include the dtb
 physical address
 */
error_t arch_start_platform(struct setup_info *arch_setup_info)
{
        struct allocator *malloc = per_cpu(kallocator, BSP_ID);
        struct fdt_header *dtb_header_ptr =
                (struct fdt_header *)(arch_setup_info
                                              ->boot_dtb_header_base_addr);
        device_root =
                build_device_tree(malloc, NULL, (void *)dtb_header_ptr, 0, 0);
        // print_device_tree(device_root);
        struct device_node *cmdline_node =
                dev_node_find_by_name(device_root, "chosen");
        if (!cmdline_node) {
                print("no input cmdline\n");
        } else {
                struct property *curr_property = cmdline_node->property;

                while (curr_property) {
                        if (curr_property->name
                            && !strcmp(curr_property->name, "bootargs")) {
                                cmdline_ptr = (char *)curr_property->data;
                                break;
                        }
                        curr_property = curr_property->next;
                }
                if (cmdline_ptr)
                        print("cmdline:%s\n", cmdline_ptr);
                else
                        print("no input cmdline\n");
        }
        psci_init();
        arch_enumerate_cpus();
        error_t e = gic_probe();
        if (e != REND_SUCCESS)
                return e;
        gic.init_distributor();
        return irq_space_gic_spi_register(gic.nr_spi());
}
/*
 * @brief the arch_start_core function is used for each core start,
 * arch_start_platform only start once, but the arch_start_cpu start for each
 * core
 * @param cpu_id point out which cpu core we try to start
 */
error_t arch_start_core(cpu_id_t cpu_id)
{
        /*write in the cpuid*/
        per_cpu(cpu_number, cpu_id) = cpu_id;
        isb();
        init_interrupt();
        gic.init_cpu_interface();
        smp_ipi_init();
        rendezvos_time_init();
        init_syscall();
        return REND_SUCCESS;
}
