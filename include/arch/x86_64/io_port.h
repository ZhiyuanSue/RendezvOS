#ifndef _SHAMPOOS_X86_64_IO_PORT_H_
#define _SHAMPOOS_X86_64_IO_PORT_H_

/*
	define some of the ports in X86_64 here, which might be platform decided
	I have find some of the website for most accepted x86 PC port definatioins
	here are two of them:
	https://bochs.sourceforge.io/techspec/PORTS.LST
	http://www.os2site.com/sw/info/memory/ports.txt
*/
#define _X86_16550A_COM1_BASE_ 0x3F8
/*actually using early serial ouput we just need one serial port*/
#define _X86_16550A_COM2_BASE_ 0x2F8

#define _X86_INIT_REGISTER_ 0x92
#define _X86_RESET_CONTROL_REGISTER_ 0xCF9

#define _X86_POWER_MANAGEMENT_ENABLE_ 0x802
/*0x802 and 0x803 is the enable registers, two bytes*/
#define _X86_POWER_MANAGEMENT_CONTROL_ 0x804
/*0x804 and 0x805 is the ctrl registers,two bytes*/

#define _X86_POWER_SHUTDOWN_ 0x604

#define _X86_8259A_MASTER_0_ 0x20
/*the even port of master 8259A*/
#define _X86_8259A_MASTER_1_ 0x21
/*the odd port of master 8259A*/

#define _X86_8259A_SLAVE_0_ 0xA0
/*the even port of slave 8259A*/
#define _X86_8259A_SLAVE_1_ 0xA1
/*the odd port of slave 8259A*/

#define _X86_8254_COUNTER_0_ 0x40
#define _X86_8254_COUNTER_1_ (_X86_8254_COUNTER_0_ + 1)
#define _X86_8254_COUNTER_2_ (_X86_8254_COUNTER_0_ + 2)
#define _X86_8254_CTRL_PORT_ (_X86_8254_COUNTER_0_ + 3)

#endif