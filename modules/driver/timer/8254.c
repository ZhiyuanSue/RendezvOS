#ifdef _X86_64_
#include <arch/x86_64/io.h>
#include <arch/x86_64/io_port.h>
#include <modules/driver/timer/8254.h>

void init_8254_cyclical(int freq)
{
        u16 t = PIT_TICK_RATE / freq;
        u8 ctrl_data;

        ctrl_data = (0 << _8254_CTL_SC_OFF_) & _8254_CTL_SC_MASK_;
        ctrl_data |= (3 << _8254_CTL_RW_OFF_) & _8254_CTL_RW_MASK_;
        ctrl_data |= (2 << _8254_CTL_M_OFF_) & _8254_CTL_M_MASK_;
        outb(_X86_8254_CTRL_PORT_, ctrl_data);
        outb(_X86_8254_COUNTER_0_, (t & 0xff));
        outb(_X86_8254_COUNTER_0_, ((t >> 8) & 0xff));
}
void init_8254_one_shot(u16 t)
{
        u8 ctrl_data;

        ctrl_data = (0 << _8254_CTL_SC_OFF_) & _8254_CTL_SC_MASK_;
        ctrl_data |= (3 << _8254_CTL_RW_OFF_) & _8254_CTL_RW_MASK_;
        ctrl_data |= (0 << _8254_CTL_M_OFF_) & _8254_CTL_M_MASK_;
        outb(_X86_8254_CTRL_PORT_, ctrl_data);
        outb(_X86_8254_COUNTER_0_, (t & 0xff));
        outb(_X86_8254_COUNTER_0_, ((t >> 8) & 0xff));
}
inline void init_8254_read()
{
        u8 ctrl_data;

        ctrl_data = 0;
        outb(_X86_8254_CTRL_PORT_, ctrl_data);
}
inline u16 read_8254_val()
{
        u16 val = 0;
        val = inb(_X86_8254_COUNTER_0_);
        val |= inb(_X86_8254_COUNTER_0_) << 8;
        return val;
}
inline void PIT_mdelay(int ms)
{
        /*
                for pic, only 16 bits, and the max is 65535
                so if the tick is 1193181 / 1000 per ms
                we can only count 50 time (59659)  every time
                however，here we use i16,so only half can we use
                so at most 25ms
        */
        init_8254_one_shot((PIT_TICK_RATE * ms) / 1000);
        init_8254_read();
        i16 t = read_8254_val();
        while (t >= 0) {
                t = read_8254_val();
        }
}
#endif