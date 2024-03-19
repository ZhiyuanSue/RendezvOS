#include <shampoos/common.h>
#include <modules/driver/uart/uart_16550A.h>
#include <modules/log/log.h>
void cmain(struct setup_info* arch_setup_info){
	uart_16550A_open();
	char* test_s="string ok";
	log_init((void*)(arch_setup_info->log_buffer_addr),&uart_16550A_putc);
	printk("ShamPoOS\n");
	printk("test printk s %s\n",test_s);
	printk("test printk x %x\n",12345);
	printk("test printk d %d\n",12345);
	printk("test printk u %u\n",12345);
	start_arch(arch_setup_info);
}