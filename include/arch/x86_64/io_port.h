#ifndef	_SHAMPOOS_X86_64_IO_PORT_H_
#define _SHAMPOOS_X86_64_IO_PORT_H_

/*define some of the ports in X86_64 here*/
#define	_X86_16550A_COM1_BASE_	0x3F8	/*actually using early serial ouput we just need one serial port*/
#define	_X86_16550A_COM2_BASE_	0x2F8

#define	_X86_INIT_REGISTER_	0x92
#define	_X86_RESET_CONTROL_REGISTER_	0xCF9

#define	_X86_POWER_MANAGEMENT_ENABLE_	0x802	/*0x802 and 0x803 is the enable registers, two bytes*/
#define _X86_POWER_MANAGEMENT_CONTROL_	0x804	/*0x804 and 0x805 is the ctrl registers,two bytes*/

#define _X86_POWER_SHUTDOWN_	0x604

#endif