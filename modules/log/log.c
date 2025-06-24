#include <common/types.h>
#include <modules/driver/driver.h>
#include <modules/log/log.h>

struct log_buffer LOG_BUFFER;
DEFINE_PER_CPU(struct spin_lock_t, log_spin_lock);
spin_lock log_spin_lock_ptr = NULL;
#ifdef _LOG_OFF_
int log_level = LOG_OFF;
#elif defined _LOG_EMERG_
int log_level = LOG_EMERG;
#elif defined _LOG_ALERT_
int log_level = LOG_ALERT;
#elif defined _LOG_CRIT_
int log_level = LOG_CRIT;
#elif defined _LOG_ERROR_
int log_level = LOG_ERROR;
#elif defined _LOG_WARNING_
int log_level = LOG_WARNING;
#elif defined _LOG_NOTICE_
int log_level = LOG_NOTICE;
#elif defined _LOG_INFO_
int log_level = LOG_INFO;
#elif defined _LOG_DEBUG_
int log_level = LOG_DEBUG;
#else
int log_level = LOG_OFF;
#endif

void log_init(void *log_buffer_addr, int log_level)
{
        uart_putc('\n');
        for (int i = 0; i < LOG_BUFFER_SIZE; ++i) {
                LOG_BUFFER.LOG_BUF[i].start_addr =
                        log_buffer_addr + i * LOG_BUFFER_SINGLE_SIZE;
                LOG_BUFFER.LOG_BUF[i].length = LOG_BUFFER_SINGLE_SIZE;
        }
        LOG_BUFFER.log_level = log_level;
        LOG_BUFFER.cur_buffer_idx = 0;
        LOG_BUFFER.cur_buffer_offset = 0;
}
void itoa(char *buf, int base, i64 d)
{
        char *p;
        unsigned long ud;
        int divisor;
        int remainder;
        char tmp;

        p = buf;
        char *p1, *p2;
        ud = d;
        divisor = 10;
        if (base == 'd' && d < 0) {
                *p++ = '-';
                buf++;
                ud = -d;
        } else if (base == 'x')
                divisor = 16;
        do {
                remainder = ud % divisor;
                *p++ = (remainder < 10) ? remainder + '0' :
                                          remainder + 'a' - 10;
        } while (ud /= divisor);
        *p = 0;
        p1 = buf;
        p2 = p - 1;
        while (p1 < p2) {
                tmp = *p1;
                *p1 = *p2;
                *p2 = tmp;
                p1++;
                p2--;
        }
}

void log_print(char *buffer, const char *format, va_list arg_list)
{
        u_int8_t c;
        char buf[20];
        int pad0;
        int pad;
        i32 d;
        u32 u;
        i64 x;

        while ((c = *format++) != 0) {
                if (c != '%'){
                        uart_putc(c);
                        char_console_putc(&X86_CHAR_CONSOLE,c);
                }
                else {
                        char *p, *p2;
                        pad0 = 0, pad = 0;
                        c = *format++;
                        if (c == '0') {
                                pad0 = 1;
                                c = *format++;
                        }
                        if (c >= '0' && c <= '9') {
                                pad = c - '0';
                                c = *format++;
                        }
                        switch (c) {
                        case 'd': {
                                d = va_arg(arg_list, int32_t);
                                itoa(buf, c, d);
                                p = buf;
                                goto string;
                        }
                        case 'u': {
                                u = va_arg(arg_list, u_int64_t);
                                itoa(buf, c, u);
                                p = buf;
                                goto string;
                        }
                        case 'x': {
                                x = va_arg(arg_list, int64_t);
                                itoa(buf, c, x);
                                p = buf;
                                goto string;
                        }
                        case 'c': {
                                uint8_t tmp_c = (uint8_t)va_arg(arg_list, int);
                                *buf = tmp_c;
                                *(buf + 1) = '\0';
                                p = buf;
                                goto string;
                        }
                        case 's':
                                p = va_arg(arg_list, char *);
                                if (!p)
                                        p = "(null)";
                        string:
                                for (p2 = p; *p2; p2++)
                                        ;
                                for (; p2 < p + pad; p2++){
                                        uart_putc(pad0 ? '0' : ' ');
                                        char_console_putc(&X86_CHAR_CONSOLE,c);
                                }
                                while (*p){
                                        uart_putc(*p++);
                                        char_console_putc(&X86_CHAR_CONSOLE,c);
                                }
                                break;
                        default:
                                break;
                        }
                }
        }
}

void printk(const char *format, int log_level, ...)
{
        va_list arg_list;

        if (log_level <= LOG_BUFFER.log_level) {
                va_start(arg_list, log_level);
                log_print(
                        LOG_BUFFER.LOG_BUF[LOG_BUFFER.cur_buffer_idx].start_addr
                                + LOG_BUFFER.cur_buffer_offset,
                        format,
                        arg_list);
                va_end(arg_list);
        }
}
