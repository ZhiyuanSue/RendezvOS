#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>
#include <modules/driver/timer/8254.h>

void	init_8254(void)
{
	u8 ctrl_data = (0 << _8254_CTL_SC_OFF_) & _8254_CTL_SC_MASK_;
	ctrl_data |= (3 << _8254_CTL_RW_OFF_) & _8254_CTL_RW_MASK_;
	ctrl_data |= (2 << _8254_CTL_M_OFF_) & _8254_CTL_M_MASK_;
	outb(_X86_8254_CTRL_PORT_, ctrl_data);
	outb(_X86_8254_COUNTER_0_, 0x23);
	outb(_X86_8254_COUNTER_0_, 0x45);
}