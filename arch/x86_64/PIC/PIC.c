#include <arch/x86_64/PIC/PIC.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>

void init_PIC() {
	/*ICW 1*/
	outb(_X86_8259A_MASTER_0_ , (_8259A_ICW_1_IC4_|_8259A_ICW_1_D4_));
	outb(_X86_8259A_SLAVE_0_ , (_8259A_ICW_1_IC4_|_8259A_ICW_1_D4_));
	/*ICW 2*/
	outb(_X86_8259A_MASTER_1_ , 0x20 & _8259A_ICW_2_VEC_HIGH_MASK_);
	outb(_X86_8259A_SLAVE_1_ , 0x28 & _8259A_ICW_2_VEC_HIGH_MASK_);
	/*ICW 3*/
	outb(_X86_8259A_MASTER_1_ , _8259A_SLAVE_MASK_);
	outb(_X86_8259A_SLAVE_1_ , _8259A_SLAVE_INDEX_);
	/*ICW 4*/
	outb(_X86_8259A_MASTER_1_ , _8259A_ICW_4_uPM_);
	outb(_X86_8259A_SLAVE_1_ , _8259A_ICW_4_uPM_);

	/*OCW 1 set all int masked*/
	outb(_X86_8259A_MASTER_1_ , 0xff);
	outb(_X86_8259A_SLAVE_1_ , 0xff);
}

void enable_IRQ(int irq_num){
	/*just clear the mask of the OCW 1*/
}

void clear_IRQ(){
	/*just send an EOI to OCW 2*/
}