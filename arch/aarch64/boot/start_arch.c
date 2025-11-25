#include <arch/aarch64/boot/arch_setup.h>
#include <arch/aarch64/mm/page_table_def.h>
#include <arch/aarch64/mm/vmm.h>
#include <arch/aarch64/power_ctrl.h>
#include <arch/aarch64/cpuinfo.h>
#include <arch/aarch64/gic/gic_v2.h>
#include <common/endianness.h>
#include <common/mm.h>
#include <modules/dtb/dtb.h>
#include <modules/dtb/print_property.h>
#include <modules/log/log.h>
#include <modules/psci/psci.h>
#include <rendezvos/error.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/spmalloc.h>
#include <rendezvos/trap.h>
#include <rendezvos/time.h>

extern u64 L2_table;
u32 BSP_ID = 0;
extern struct allocator *kallocator;
struct cpuinfo cpu_info = {0};

extern void syscall(struct trap_frame *syscall_ctx);
/*
 * @brief the init_syscall function is register the syscall function as the 0x15
 irq under the AArch64 architecture, this irq means syscall
 */
static void init_syscall(void)
{
        /*TODO:0x15 need defined by macros*/
        register_irq_handler(0x15, syscall, IRQ_NO_ATTR);
}
/*
 * @brief get_cpu_info read some cpu register and read the cpu infomation to the
 * structure cpu_info
 */
static void get_cpu_info(void)
{
        u64 MPIDR_VAL;
        mrs("MPIDR_EL1", MPIDR_VAL);
        if (MPIDR_EL1_MT(MPIDR_VAL)) {
                cpu_info.MT = true;
        } else {
                cpu_info.MT = false;
        }
        if (MPIDR_EL1_U(MPIDR_VAL)) {
                cpu_info.MP = false;
        } else {
                cpu_info.MP = true;
        }
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
        BSP_ID = 0;
        return 0;
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
        psci_init();
        gic.probe();
        gic.init_distributor();
        return 0;
}
/*
 * @brief the arch_start_core function is used for each core start,
 * arch_start_platform only start once, but the arch_start_cpu start for each
 * core
 * @param cpu_id point out which cpu core we try to start
 */
error_t arch_start_core(int cpu_id)
{
        /*write in the cpuid*/
        msr("TPIDR_EL1", __per_cpu_offset[cpu_id]);
        per_cpu(cpu_number, cpu_id) = cpu_id;
        isb();
        init_interrupt();
        gic.init_cpu_interface();
        rendezvos_time_init();
        init_syscall();
        return (0);
}