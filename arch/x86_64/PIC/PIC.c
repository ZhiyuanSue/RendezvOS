#include <arch/x86_64/PIC/PIC.h>
#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>
#include <shampoos/error.h>

u8 _8259A_IMR_MASTER_, _8259A_IMR_SLAVE_;
void init_PIC() {
	/*ICW 1*/
	outb(_X86_8259A_MASTER_0_, (_8259A_ICW_1_IC4_ | _8259A_ICW_1_D4_));
	outb(_X86_8259A_SLAVE_0_, (_8259A_ICW_1_IC4_ | _8259A_ICW_1_D4_));
	/*ICW 2*/
	outb(_X86_8259A_MASTER_1_,
		 _8259A_MASTER_IRQ_NUM_ & _8259A_ICW_2_VEC_HIGH_MASK_);
	outb(_X86_8259A_SLAVE_1_,
		 _8259A_SLAVE_IRQ_NUM_ & _8259A_ICW_2_VEC_HIGH_MASK_);
	/*ICW 3*/
	outb(_X86_8259A_MASTER_1_, _8259A_SLAVE_MASK_);
	outb(_X86_8259A_SLAVE_1_, _8259A_SLAVE_INDEX_);
	/*ICW 4*/
	outb(_X86_8259A_MASTER_1_, _8259A_ICW_4_uPM_);
	outb(_X86_8259A_SLAVE_1_, _8259A_ICW_4_uPM_);

	/*OCW 1 set all int masked*/
	outb(_X86_8259A_MASTER_1_, 0xFB);
	outb(_X86_8259A_SLAVE_1_, 0xFF);
	_8259A_IMR_MASTER_ = 0xFB;
	_8259A_IMR_SLAVE_ = 0xFF;
}

error_t enable_IRQ(int irq_num) {
	/*just clear the mask of the OCW 1*/
	if (irq_num >= _8259A_MASTER_IRQ_NUM_ &&
		irq_num < _8259A_MASTER_IRQ_NUM_ + _8259A_IRQ_NUM_) { /*master irq*/
		u8 tmp_mask = ~_8259A_IMR_MASTER_;
		tmp_mask |= 1 << (irq_num - _8259A_MASTER_IRQ_NUM_);
		_8259A_IMR_MASTER_ = ~tmp_mask;
		outb(_X86_8259A_MASTER_1_, _8259A_IMR_MASTER_);
	} else if (irq_num >= _8259A_SLAVE_IRQ_NUM_ &&
			   irq_num < _8259A_SLAVE_IRQ_NUM_ +
							 _8259A_IRQ_NUM_) { /*slave irq, need enable the irq
												   2 at master, too*/
		u8 tmp_mask = ~_8259A_IMR_SLAVE_;
		tmp_mask |= 1 << (irq_num - _8259A_SLAVE_IRQ_NUM_);
		_8259A_IMR_SLAVE_ = ~tmp_mask;
		outb(_X86_8259A_SLAVE_1_, _8259A_IMR_SLAVE_);
	} else { /*wrong irq num*/
		return -EPERM;
	}
	return 0;
}
error_t disable_IRQ(int irq_num) {
	if (irq_num >= _8259A_MASTER_IRQ_NUM_ &&
		irq_num < _8259A_MASTER_IRQ_NUM_ + _8259A_IRQ_NUM_) { /*master irq*/
		_8259A_IMR_MASTER_ |= 1 << (irq_num - _8259A_MASTER_IRQ_NUM_);
		outb(_X86_8259A_MASTER_1_, _8259A_IMR_MASTER_);
	} else if (irq_num >= _8259A_SLAVE_IRQ_NUM_ &&
			   irq_num <
				   _8259A_SLAVE_IRQ_NUM_ + _8259A_IRQ_NUM_) { /*slave irq*/
		_8259A_IMR_SLAVE_ |= 1 << (irq_num - _8259A_SLAVE_IRQ_NUM_);
		outb(_X86_8259A_SLAVE_1_, _8259A_IMR_SLAVE_);
	} else { /*wrong irq num*/
		return -EPERM;
	}
	return 0;
}

error_t EOI(int irq_num) { /*just send an EOI to OCW 2*/
	if (irq_num >= _8259A_MASTER_IRQ_NUM_ &&
		irq_num < _8259A_MASTER_IRQ_NUM_ + _8259A_IRQ_NUM_) { /*master irq*/
		outb(_X86_8259A_MASTER_0_, _8259A_OCW_2_EOI_);
	} else if (irq_num >= _8259A_SLAVE_IRQ_NUM_ &&
			   irq_num <
				   _8259A_SLAVE_IRQ_NUM_ + _8259A_IRQ_NUM_) { /*slave irq*/
		outb(_X86_8259A_SLAVE_0_, _8259A_OCW_2_EOI_);
	} else { /*wrong irq num*/
		return -EPERM;
	}
	return 0;
}