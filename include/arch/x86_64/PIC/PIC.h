#ifndef _SHAMPOOS_PIC_H_
# define _SHAMPOOS_PIC_H_
/*
	pic is also called 8259A, the even port and odd port is defined in
   ../io_port.h I think it's platform related
*/

/*ICW 1, even*/
# define _8259A_ICW_1_IC4_ (1 << 0) /*D0, 1 means need ICW4*/
# define _8259A_ICW_1_SNGL_ \
	(1 << 1) /*D1,         \
1 means single 8259A,0 means multipul*/
# define _8259A_ICW_1_LTIM_              \
	(1 << 3)                      /*D3, \
 1 means level trigged,0 means edge-trigged*/
# define _8259A_ICW_1_D4_ (1 << 4) /*always 1 to indicate it's ICW 1*/
/*in linux 0.11 use ICW 1 = 0x11, and we also try to use the 2 8259A*/

/*ICW 2, odd*/
# define _8259A_ICW_2_VEC_HIGH_MASK_ (0x1f << 3)
/*only high 5 bit is used for intrrupt vector*/

/*ICW 3, odd*/
/*
	bit in ICW 3 means the master and slave relationship
	at master icw 3, which bit is 1 means which links the slave
	and at slave,the icw 3 should have the index at which the master bit masked
*/

/*ICW 4, odd*/
# define _8259A_ICW_4_uPM_ (1 << 0)  /*should be 1*/
# define _8259A_ICW_4_AEOI_ (1 << 1) /*1 means auto,0 means menual*/
# define _8259A_ICW_4_MS_                                      \
	(1 << 2)                        /*Under Buffering Method, \
   1 means use master, 0 means slave*/
# define _8259A_ICW_4_BUF_ (1 << 3)  /*1 means Buffering Method, 0 means no*/
# define _8259A_ICW_4_SFNM_ (1 << 4) /*unused*/

/*OCW 1, odd*/
/*mask some bit to mask the interrupt*/

/*OCW 2, even*/
# define _8259A_OCW_2_L0_ (1 << 0) /*only SL=1,used to indicate the source*/
# define _8259A_OCW_2_L1_ (1 << 1)
# define _8259A_OCW_2_L2_ (1 << 2)
# define _8259A_OCW_2_EOI_ (1 << 5) /*used for end of interrupt*/
# define _8259A_OCW_2_SL_ (1 << 6)
# define _8259A_OCW_2_R_      \
	(1 << 7) /*for priority, \
and use sl and L2-L0 to indicate the priority*/

/*OCW 3, even*/
/*
	used for interrupt polling way
	should first use 'out' instruction to set P
	and then use 'in' read it
*/
# define _8259A_OCW_3_RIS_                                              \
	(1 << 0)                      /*set RR, if RIS=0, read IRR, RIS=1, \
  read ISR*/
# define _8259A_OCW_3_RR_ (1 << 1) /*indicate the validation of read*/
# define _8259A_OCW_3_P_ (1 << 2)  /*indicate polling*/

# define _8259A_OCW_3_D3_ (1 << 3)
/*must be 1 to indicate it's OCW 3 (and D4 must be 0)*/
# define _8259A_OCW_3_SMM_ (1 << 5)
/*set ESMM,1 means the special mask method set, 0 means reset*/
# define _8259A_OCW_3_ESMM_ (1 << 6) /*set SMM validation*/

/*some IRQ number is pre-defined*/
# define _8259A_MASTER_IRQ_NUM_ (0x20)
# define _8259A_IRQ_NUM_ (8) /*The 8259A have only 8 irqs*/
# define _8259A_SLAVE_IRQ_NUM_ (_8259A_MASTER_IRQ_NUM_ + _8259A_IRQ_NUM_)
/*Master*/
# define _8259A_TIMER_ (0)
# define _8259A_KEYBOARD_ (1)
# define _8259A_SLAVE_INDEX_ (2)
# define _8259A_SLAVE_MASK_ (1 << _8259A_SLAVE_INDEX_) /*used to init ICW 3*/
# define _8259A_UART_2_ (3)
# define _8259A_UART_1_ (4)
# define _8259A_LPT_2_ (5)
# define _8259A_FLOPPY_ (6)
# define _8259A_LPT_1_ (7)
/*Slave*/
# define _8259A_RT_TIMER (8)
# define _8259A_MOUSE (12)
# define _8259A_FP (13)
# define _8259A_AT (14)

# include <common/types.h>
void	init_PIC(void);
void	disable_PIC(void);
error_t	enable_IRQ(int irq_num);
error_t	disable_IRQ(int irq_num);
error_t	EOI(int irq_num);
#endif