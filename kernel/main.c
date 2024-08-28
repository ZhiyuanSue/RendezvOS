#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
#include <shampoos/common.h>

extern int log_level;
extern char _bss_start, _bss_end;
extern char _end;
extern struct buddy buddy_pmm;

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
        if (start_arch(arch_setup_info)) {
                pr_error("error start arch\n");
                return;
        }
        buddy_pmm.pmm_init(arch_setup_info);
        /*TODO:after we init the pmm module, we can alloc some pages for
         * stack,and no more boot stack：in x86,please use LSS, see
         * manual 6.8.3*/
        start_smp();
        test();
        arch_shutdown();
}
