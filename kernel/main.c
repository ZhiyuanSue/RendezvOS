#include <shampoos/common.h>
#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>

extern	int log_level;
extern	char _bss_start,_bss_end;
extern	struct	pmm	buddy_pmm;

void cmain(struct setup_info* arch_setup_info){
	if(arch_setup_info==NULL)
		return;
	uart_open();
	log_init((void*)(arch_setup_info->log_buffer_addr),log_level,&uart_putc);
#ifdef HELLO
	hello_world();
#endif
	if(start_arch(arch_setup_info))
	{
		pr_error("error start arch\n");
		return;
	}
	buddy_pmm.pmm_init(arch_setup_info);
	/*TODO:after we init the pmm module, we can alloc some pages for stack,and no more boot stack*/

	start_smp();
	test();
	arch_shutdown();
}
