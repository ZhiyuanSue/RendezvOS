#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/cpuinfo.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/time.h>
#include <arch/x86_64/trap/trap.h>
#include <arch/x86_64/desc.h>
#include <arch/x86_64/trap/tss.h>
#include <arch/x86_64/msr.h>
#include <shampoos/percpu.h>
#include <modules/driver/timer/8254.h>
#include <modules/log/log.h>
#include <modules/acpi/acpi.h>
#include <shampoos/error.h>
#include <shampoos/mm/vmm.h>
#include <shampoos/mm/nexus.h>

extern u32 max_phy_addr_width;
struct cpuinfo cpu_info;
int BSP_ID;
extern struct nexus_node *nexus_root;
extern struct pseudo_descriptor gdt_desc;
extern union desc gdt[GDT_SIZE];
static void get_cpu_info(void)
{
        u32 eax;
        u32 ebx;
        u32 ecx;
        u32 edx;

        /*TODO :rewite the check of cpuid*/
        /*first get the number that cpuid support*/
        cpuid(0x0, &eax, &ebx, &ecx, &edx);
        cpuid(0x1, &eax, &ebx, &ecx, &edx);
        cpu_info.APICID = ebx >> 24;
        cpu_info.feature_1 = ecx;
        cpu_info.feature_2 = edx;
        /*detect invariant tsc*/
        cpuid(X86_CPUID_Invariant_TSC, &eax, &ebx, &ecx, &edx);
        if (edx & X86_CPUID_Invariant_TSC_EDX) {
                cpu_info.invariant_tsc_support = 1;
        } else {
                cpu_info.invariant_tsc_support = 0;
        }
}
static void enable_cache(void)
{
}
static void start_fp(void)
{
        set_cr0_bit(CR0_MP);
        set_cr0_bit(CR0_NE);
}
static void start_simd(void)
{
        u64 xcr_value;

        /*use cpuinfo to check the simd support or not*/
        if (((cpu_info.feature_2)
             & (X86_CPUID_FEATURE_EDX_SSE | X86_CPUID_FEATURE_EDX_SSE2
                | X86_CPUID_FEATURE_EDX_FXSR | X86_CPUID_FEATURE_EDX_CLFSH))
            && ((cpu_info.feature_1)
                & (X86_CPUID_FEATURE_ECX_SSE3 | X86_CPUID_FEATURE_ECX_SSSE3))) {
                pr_info("have simd feature,starting...\n");
                /*set osfxsr : cr4 bit 9*/
                set_cr4_bit(CR4_OSFXSR);
                /*set osxmmexcpt flag: cr4 bit 10*/
                set_cr4_bit(CR4_OSXMMEXCPT);
                /*set the mask bit and flags in mxcsr register*/
                set_mxcsr(MXCSR_IM | MXCSR_DM | MXCSR_ZM | MXCSR_OM | MXCSR_UM
                          | MXCSR_PM);
                /*the following codes seems useless for enable sse,emmm*/
                if ((cpu_info.feature_1) & X86_CPUID_FEATURE_ECX_XSAVE) {
                        /*to enable the xcr0, must set the cr4 osxsave*/
                        set_cr4_bit(CR4_OSXSAVE);
                        xcr_value = get_xcr(0);
                        set_xcr(0, xcr_value | XCR0_X87 | XCR0_SSE | XCR0_AVX);
                }
        } else {
                goto start_simd_fail;
        }
        return;
start_simd_fail:
        pr_error("start simd fail\n");
}
static void prepare_per_cpu_new_gdt(struct pseudo_descriptor *desc,
                                    union desc *gdt)
{
        /*fill in the gdt table*/
        gdt[1].seg_desc.type = 0xe;
        gdt[1].seg_desc.p = 1;
        gdt[1].seg_desc.s = 1;
        gdt[1].seg_desc.l = 1;

        /*fill in the gdt desc*/
        desc->base_addr = (u64)gdt;
        desc->limit = GDT_SIZE * sizeof(union desc) - 1;
}
error_t prepare_arch(struct setup_info *arch_setup_info)
{
        u32 mtb_magic;
        struct multiboot_info *mtb_info;

        mtb_magic = arch_setup_info->multiboot_magic;
        mtb_info = GET_MULTIBOOT_INFO(arch_setup_info);
        if (mtb_magic != MULTIBOOT_MAGIC) {
                pr_info("not using the multiboot protocol, stop\n");
                return (-EPERM);
        }
        pr_info("finish check the magic:%x\n", mtb_magic);
        max_phy_addr_width = arch_setup_info->phy_addr_width;
        if (!(mtb_info->flags & MULTIBOOT_INFO_FLAG_CMD)) {
                pr_info("cmdline:%s\n",
                        (char *)(mtb_info->cmdline + KERNEL_VIRT_OFFSET));
        } else {
                pr_info("no input cmdline\n");
        }

        return (0);
}
error_t arch_cpu_info(struct setup_info *arch_setup_info)
{
        get_cpuinfo();
        BSP_ID = cpu_info.APICID;
        return 0;
}
error_t arch_parser_platform(struct setup_info *arch_setup_info)
{
        error_t e = acpi_init(arch_setup_info->rsdp_addr);

        return e;
}
error_t start_arch(int cpu_id)
{
        union desc *per_cpu_gdt = per_cpu(gdt, cpu_id);
        /*gdt*/
        prepare_per_cpu_new_gdt(&per_cpu(gdt_desc, cpu_id), per_cpu_gdt);
        lgdt(&per_cpu(gdt_desc, cpu_id));
        /*tss*/
        prepare_per_cpu_tss_desc(&((per_cpu_gdt)[GDT_TSS_LOWER_INDEX]),
                                 &((per_cpu_gdt)[GDT_TSS_UPPER_INDEX]),
                                 cpu_id);
        /*
         gs
         as it's per_cpu base
         only after we set the gs base can we use percpu
        */
        wrmsr(MSR_GS_BASE, __per_cpu_offset[cpu_id]);
        /*table_indicator = 1  will cause #GP*/
        prepare_per_cpu_tss(per_cpu(nexus_root, cpu_id));

        init_interrupt();
        init_irq();
        sti();
        shampoos_time_init();
        enable_cache();
        start_fp();
        start_simd();
        return (0);
}
