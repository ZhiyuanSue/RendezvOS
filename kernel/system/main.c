#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
#include <rendezvos/common.h>
#include <rendezvos/mm/pmm.h>
#include <rendezvos/task/tcb.h>
#include <rendezvos/task/initcall.h>
#include <arch/aarch64/sys_ctrl.h>
#include <rendezvos/system/panic.h>
extern int log_level;
extern char _bss_start, _bss_end;
extern char _end;
extern cpu_id_t BSP_ID;

void cmain(struct setup_info *arch_setup_info)
{
        if (arch_setup_info == NULL)
                kernel_panic("cmain: setup_info is NULL");
        /*for mmio way, we map the uart after the rount up pos of end,for
         * x86,use io port*/
        uart_open((void *)ROUND_UP((vaddr)(&_end), MIDDLE_PAGE_SIZE));
        log_init((void *)(arch_setup_info->log_buffer_addr), log_level);
#ifdef HELLO
        hello_world();
#endif
        if (prepare_arch(arch_setup_info)) {
                kernel_panic("[ERROR]cmain: prepare_arch failed");
        }
        if (phy_mm_init(arch_setup_info)) {
                kernel_panic("[ERROR]cmain: phy_mm_init failed");
        }
        arch_enable_percpu(BSP_ID);
        if (arch_cpu_info(arch_setup_info)) {
                kernel_panic("[ERROR]cmain: arch_cpu_info failed");
        }
        if (virt_mm_init(BSP_ID, arch_setup_info)) {
                kernel_panic("[ERROR]cmain: virt_mm_init failed");
        }
        if (arch_start_platform(arch_setup_info)) {
                kernel_panic("[ERROR]cmain: arch_start_platform failed");
        }
        /*TODO:after we init the pmm module, we can alloc some pages for
         * stack,and no more boot stack：in x86,please use LSS, see
         * manual 6.8.3*/
        if (arch_start_core(BSP_ID)) {
                kernel_panic("[ERROR]cmain: arch_start_core failed");
        }
        init_id_managers();
        if (global_port_init()) {
                kernel_panic("[ERROR]cmain: global_port_init failed");
        }

        percpu(core_tm) = init_proc();
        if (!percpu(core_tm)) {
                kernel_panic("cmain: init_proc failed");
        }

        do_init_call();
        start_smp(arch_setup_info);
#ifdef RENDEZVOS_TEST
        create_test_thread(true);
        thread_set_status(get_cpu_current_thread(), thread_status_suspend);
#endif
        while (1) {
                schedule(percpu(core_tm));
                arch_cpu_relax();
        }
}

