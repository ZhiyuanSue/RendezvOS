#include <arch/x86_64/PIC/IRQ.h>
#include <arch/x86_64/boot/arch_setup.h>
#include <arch/x86_64/boot/multiboot.h>
#include <arch/x86_64/boot/multiboot2.h>
#include <arch/x86_64/cpuinfo.h>
#include <arch/x86_64/sys_ctrl.h>
#include <arch/x86_64/sys_ctrl_def.h>
#include <arch/x86_64/time.h>
#include <arch/x86_64/desc.h>
#include <arch/x86_64/trap/tss.h>
#include <arch/x86_64/msr.h>
#include <modules/driver/timer/8254.h>
#include <modules/log/log.h>
#include <modules/acpi/acpi.h>
#include <rendezvos/smp/percpu.h>
#include <rendezvos/error.h>
#include <rendezvos/mm/vmm.h>
#include <rendezvos/mm/nexus.h>
#include <rendezvos/trap.h>

extern u32 max_phy_addr_width;
struct cpuinfo cpu_info = {0};
int BSP_ID = 0;
extern struct nexus_node *nexus_root;
extern struct pseudo_descriptor gdt_desc;
extern union desc gdt[GDT_SIZE];
void prepare_per_cpu_new_gdt(struct pseudo_descriptor *desc, union desc *gdt);
void perpare_per_cpu_user_gdt(union desc *gdt);
extern void arch_set_syscall_entry();
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
static void init_syscall(void)
{
        /*set the IA32_STAR register*/
        u64 ia32_star_val = ((GDT_KERNEL_CS_INDEX & 0xFF) << 32)
                            + ((GDT_TSS_UPPER_INDEX & 0xFF) << 48);
        wrmsr(MSR_IA32_STAR, ia32_star_val);
        /*set the MSR_IA32_FMASK register*/
        wrmsr(MSR_IA32_FMASK, 0x200UL);
        /*set the MSR_IA32_LSTAR register*/
        arch_set_syscall_entry();
}
static void set_gdt(int cpu_id)
{
        union desc *per_cpu_gdt = per_cpu(gdt, cpu_id);
        /*gdt*/
        prepare_per_cpu_new_gdt(&per_cpu(gdt_desc, cpu_id), per_cpu_gdt);
        lgdt(&per_cpu(gdt_desc, cpu_id));
        /*tss*/
        prepare_per_cpu_tss_desc(&((per_cpu_gdt)[GDT_TSS_LOWER_INDEX]),
                                 &((per_cpu_gdt)[GDT_TSS_UPPER_INDEX]),
                                 cpu_id);
        /*user*/
        perpare_per_cpu_user_gdt(per_cpu_gdt);
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
error_t prepare_arch(struct setup_info *arch_setup_info)
{
        u32 mtb_magic;
        max_phy_addr_width = arch_setup_info->phy_addr_width;

        mtb_magic = arch_setup_info->multiboot_magic;
        if (mtb_magic == MULTIBOOT_MAGIC) {
                pr_info("using multiboot 1\n");
                struct multiboot_info *mtb_info =
                        GET_MULTIBOOT_INFO(arch_setup_info);
                if (!(mtb_info->flags & MULTIBOOT_INFO_FLAG_CMD)) {
                        pr_info("cmdline:%s\n",
                                (char *)(mtb_info->cmdline
                                         + KERNEL_VIRT_OFFSET));
                } else {
                        pr_info("no input cmdline\n");
                }
        } else if (mtb_magic == MULTIBOOT2_MAGIC) {
                pr_info("using multiboot 2\n");
                struct multiboot2_info *mtb2_info =
                        GET_MULTIBOOT2_INFO(arch_setup_info);
                bool have_cmd_line = false;
                for_each_tag(mtb2_info)
                {
                        switch (tag->type) {
                        case MULTIBOOT2_TAG_TYPE_CMDLINE: {
                                have_cmd_line = true;
                                pr_info("cmdline:%s\n",
                                        ((struct multiboot2_tag_string *)tag)
                                                ->string);
                        } break;
                        }
                }
                if (!have_cmd_line)
                        pr_info("no input cmdline\n");
        } else {
                pr_info("not using the multiboot protocol, stop\n");
                return (-E_RENDEZVOS);
        }
        return (0);
}
error_t arch_cpu_info(struct setup_info *arch_setup_info)
{
        get_cpu_info();
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
        set_gdt(cpu_id);
        /*
         gs
         as it's per_cpu base
         only after we set the gs base can we use percpu
        */
        wrmsr(MSR_GS_BASE, __per_cpu_offset[cpu_id]);
        wrmsr(MSR_KERNEL_GS_BASE, 0);
        per_cpu(cpu_number, cpu_id) = cpu_id;
        /*table_indicator = 1  will cause #GP*/
        prepare_per_cpu_tss(per_cpu(nexus_root, cpu_id));

        init_interrupt();
        init_irq();
        init_syscall();
        sti();
        rendezvos_time_init();
        enable_cache();
        start_fp();
        start_simd();
        return (0);
}
