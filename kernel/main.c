#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
#include <rendezvos/common.h>
#include <rendezvos/task/tcb.h>
#include <arch/aarch64/sys_ctrl.h>
extern int log_level;
extern char _bss_start, _bss_end;
extern char _end;
extern int BSP_ID;

void cmain(struct setup_info *arch_setup_info)
{
        if (arch_setup_info == NULL)
                return;
        /*for mmio way, we map the uart after the rount up pos of end,for
         * x86,use io port*/
        uart_open((void *)ROUND_UP((vaddr)(&_end), MIDDLE_PAGE_SIZE));
        log_init((void *)(arch_setup_info->log_buffer_addr), log_level);
#ifdef HELLO
        hello_world();
#endif
        if (prepare_arch(arch_setup_info)) {
                print("[ERROR] prapare arch\n");
                return;
        }
        if (phy_mm_init(arch_setup_info)) {
                print("[ERROR] phy mm init error\n");
                return;
        }
        if (arch_cpu_info(arch_setup_info)) {
                print("[ERROR] arch cpu info error\n");
                return;
        }
        if (virt_mm_init(BSP_ID, arch_setup_info)) {
                print("[ERROR] virt mm init error\n");
                return;
        }
        fb2_map_pages();
        fb2_show();
        if (arch_parser_platform(arch_setup_info)) {
                print("[ERROR] arch parser platform\n");
                return;
        }
        /*TODO:after we init the pmm module, we can alloc some pages for
         * stack,and no more boot stack：in x86,please use LSS, see
         * manual 6.8.3*/
        if (start_arch(BSP_ID)) {
                print("[ERROR] start arch\n");
                return;
        }

        percpu(core_tm) = init_proc();

        main_init();
        start_smp(arch_setup_info);
#ifdef TEST
        BSP_test();
#endif
        arch_shutdown();
}