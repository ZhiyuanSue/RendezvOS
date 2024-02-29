#include "sbi.h"
/* This sbi_ecall references Linux's implement*/
struct sbiret sbi_ecall(int eid,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
                        unsigned long arg5)
{
    struct sbiret ret;
    register unsigned long a0 asm ("a0") = arg0;
    register unsigned long a1 asm ("a1") = arg1;
    register unsigned long a2 asm ("a2") = arg2;
    register unsigned long a3 asm ("a3") = arg3;
    register unsigned long a4 asm ("a4") = arg4;
    register unsigned long a5 asm ("a5") = arg5;
    register unsigned long a6 asm ("a6") = (unsigned long)fid;
    register unsigned long a7 asm ("a7") = (unsigned long)eid;
    __asm__ __volatile__ (
        "ecall"
        :"+r"(a0),"+r"(a1)
        :"r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6), "r" (a7)
        :"memory");
    ret.error   =   a0;
    ret.value   =   a1;
    return ret;
}



struct sbiret sbi_debug_console_write(unsigned long num_bytes,
                                      unsigned long base_addr_lo,
                                      unsigned long base_addr_hi)
{
    return sbi_ecall(SBI_DBCN_EXT,SBI_EXT_DBCN_CONSOLE_WRITE,0,0,0,0,0,0);
}
struct sbiret sbi_debug_console_write_byte(u_int8_t byte)
{
    return sbi_ecall(SBI_DBCN_EXT,SBI_EXT_DBCN_CONSOLE_WRITE_BYTE,0,0,0,0,0,0);
}

#ifdef _SBI_LEGANCY_
long sbi_console_putchar(int ch)
{
    struct sbiret ret;
    ret=sbi_ecall(SBI_LEGANCY_EXT_CONSOLE_PUTCHAR,0,0,0,0,0,0,0);
    return ret.error;
}

void sbi_shutdown(void)
{
    sbi_ecall(SBI_LEGANCY_EXT_SHUTDOWN,0,0,0,0,0,0,0);
}
#endif
void sbi_init(void)
{
    
}
struct sbiret sbi_system_reset(u_int32_t reset_type, u_int32_t reset_reason)
{
    return sbi_ecall(SBI_SRST_EXT,SBI_EXT_SRST_RESET,(unsigned long)reset_type,(unsigned long)reset_reason,0,0,0,0);
}

