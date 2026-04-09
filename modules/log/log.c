#include <common/types.h>
#include <common/string.h>
#include <common/stdarg.h>
#include <modules/log/log.h>
#include <modules/driver/uart/uart.h>
#include <rendezvos/smp/percpu.h>

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

void log_init(void *log_buffer_addr, u64 log_level)
{
        uart_putc('\n');
        CONSOLE_CLEAN_SCREEN(&X86_CHAR_CONSOLE);
        for (int i = 0; i < LOG_BUFFER_SIZE; ++i) {
                LOG_BUFFER.LOG_BUF[i].start_addr =
                        log_buffer_addr + i * LOG_BUFFER_SINGLE_SIZE;
                LOG_BUFFER.LOG_BUF[i].length = LOG_BUFFER_SINGLE_SIZE;
        }
        LOG_BUFFER.log_level = log_level;
        LOG_BUFFER.cur_buffer_idx = 0;
        LOG_BUFFER.cur_buffer_offset = 0;
}
/* Convert unsigned integer to string with given base (2-36) */
static void uitostr(char *buf, u64 value, int base, int uppercase)
{
        static const char *lower_digits =
                "0123456789abcdefghijklmnopqrstuvwxyz";
        static const char *upper_digits =
                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const char *digits = uppercase ? upper_digits : lower_digits;
        char *p = buf;

        if (base < 2 || base > 36) {
                *p++ = '?';
                *p = '\0';
                return;
        }

        do {
                *p++ = digits[value % base];
                value /= base;
        } while (value != 0);
        *p = '\0';

        /* Reverse string */
        char *p1 = buf;
        char *p2 = p - 1;
        while (p1 < p2) {
                char tmp = *p1;
                *p1 = *p2;
                *p2 = tmp;
                p1++;
                p2--;
        }
}

/* Format specification flags */
#define FMT_FLAG_LEFT      (1 << 0) /* '-' left align */
#define FMT_FLAG_PLUS      (1 << 1) /* '+' show sign */
#define FMT_FLAG_SPACE     (1 << 2) /* ' ' space before positive */
#define FMT_FLAG_ALTERNATE (1 << 3) /* '#' alternate form */
#define FMT_FLAG_ZEROPAD   (1 << 4) /* '0' pad with zeros */

/* Length modifiers */
enum length_mod {
        LEN_NONE,
        LEN_HH,
        LEN_H,
        LEN_L,
        LEN_LL,
        LEN_J,
        LEN_Z,
        LEN_T,
        LEN_MAX
};

/* Parse a format specifier, advance the format pointer */
static const char *parse_format_spec(const char **fmt_ptr, int *flags,
                                     int *width, enum length_mod *len,
                                     char *spec)
{
        const char *p = *fmt_ptr;

        /* Parse flags */
        *flags = 0;
        while (*p) {
                switch (*p) {
                case '-':
                        *flags |= FMT_FLAG_LEFT;
                        p++;
                        break;
                case '+':
                        *flags |= FMT_FLAG_PLUS;
                        p++;
                        break;
                case ' ':
                        *flags |= FMT_FLAG_SPACE;
                        p++;
                        break;
                case '#':
                        *flags |= FMT_FLAG_ALTERNATE;
                        p++;
                        break;
                case '0':
                        *flags |= FMT_FLAG_ZEROPAD;
                        p++;
                        break;
                default:
                        goto width;
                }
        }

width:
        /* Parse width (simple decimal, no '*') */
        *width = 0;
        while (*p >= '0' && *p <= '9') {
                *width = *width * 10 + (*p - '0');
                p++;
        }

        /* Parse length modifier */
        *len = LEN_NONE;
        if (*p == 'h') {
                if (*(p + 1) == 'h') {
                        *len = LEN_HH;
                        p += 2;
                } else {
                        *len = LEN_H;
                        p++;
                }
        } else if (*p == 'l') {
                if (*(p + 1) == 'l') {
                        *len = LEN_LL;
                        p += 2;
                } else {
                        *len = LEN_L;
                        p++;
                }
        } else if (*p == 'j') {
                *len = LEN_J;
                p++;
        } else if (*p == 'z') {
                *len = LEN_Z;
                p++;
        } else if (*p == 't') {
                *len = LEN_T;
                p++;
        }

        /* Parse specifier */
        *spec = *p;
        if (*spec) {
                p++;
        }

        *fmt_ptr = p;
        return p;
}

void log_put_byte(char ch)
{
        uart_putc((u8)ch);
}

void log_put_locked(const u8 *buf, u64 len)
{
#ifdef SMP
        lock_mcs(&log_spin_lock_ptr, &percpu(log_spin_lock));
#endif
        for (u64 i = 0; i < len; i++)
                log_put_byte((char)buf[i]);
#ifdef SMP
        unlock_mcs(&log_spin_lock_ptr, &percpu(log_spin_lock));
#endif
}

/* Output a character to log sinks (see `log_put_byte`). */
static void putc_char(char ch)
{
        log_put_byte(ch);
}

/* Output a string with given length */
static void puts_len(const char *str, int len)
{
        for (int i = 0; i < len; i++) {
                putc_char(str[i]);
        }
}

/* Output a string (null-terminated) */
static void puts_str(const char *str)
{
        while (*str) {
                putc_char(*str++);
        }
}

/* Format and output a signed integer */
static void format_signed(i64 value, int base, int uppercase, int flags,
                          int width)
{
        char buf[64];
        int is_negative = 0;
        u64 uvalue;

        if (value < 0) {
                is_negative = 1;
                uvalue = -value;
        } else {
                uvalue = (u64)value;
        }

        uitostr(buf, uvalue, base, uppercase);

        char prefix[4] = {0};
        int prefix_len = 0;
        if (is_negative) {
                prefix[0] = '-';
                prefix_len = 1;
        } else if (flags & FMT_FLAG_PLUS) {
                prefix[0] = '+';
                prefix_len = 1;
        } else if (flags & FMT_FLAG_SPACE) {
                prefix[0] = ' ';
                prefix_len = 1;
        }

        if (flags & FMT_FLAG_ALTERNATE) {
                if (base == 16) {
                        prefix[prefix_len] = '0';
                        prefix[prefix_len + 1] = uppercase ? 'X' : 'x';
                        prefix_len += 2;
                } else if (base == 8) {
                        prefix[prefix_len] = '0';
                        prefix_len += 1;
                }
        }

        int num_len = strlen(buf);
        int total_len = prefix_len + num_len;
        int pad_len = width > total_len ? width - total_len : 0;

        if ((flags & FMT_FLAG_ZEROPAD) && !(flags & FMT_FLAG_LEFT)) {
                if (prefix_len > 0) {
                        puts_len(prefix, prefix_len);
                }
                for (int i = 0; i < pad_len; i++) {
                        putc_char('0');
                }
                puts_str(buf);
        } else {
                if (!(flags & FMT_FLAG_LEFT) && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                                putc_char(' ');
                        }
                }
                if (prefix_len > 0) {
                        puts_len(prefix, prefix_len);
                }
                puts_str(buf);
                if (flags & FMT_FLAG_LEFT && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                                putc_char(' ');
                        }
                }
        }
}

/* Format and output an unsigned integer */
static void format_unsigned(u64 value, int base, int uppercase, int flags,
                            int width)
{
        char buf[64];
        uitostr(buf, value, base, uppercase);

        char prefix[4] = {0};
        int prefix_len = 0;
        /* For unsigned, plus/space flags are ignored per standard */
        if (flags & FMT_FLAG_ALTERNATE) {
                if (base == 16) {
                        prefix[prefix_len] = '0';
                        prefix[prefix_len + 1] = uppercase ? 'X' : 'x';
                        prefix_len += 2;
                } else if (base == 8) {
                        prefix[prefix_len] = '0';
                        prefix_len += 1;
                }
        }

        int num_len = strlen(buf);
        int total_len = prefix_len + num_len;
        int pad_len = width > total_len ? width - total_len : 0;

        if ((flags & FMT_FLAG_ZEROPAD) && !(flags & FMT_FLAG_LEFT)) {
                if (prefix_len > 0) {
                        puts_len(prefix, prefix_len);
                }
                for (int i = 0; i < pad_len; i++) {
                        putc_char('0');
                }
                puts_str(buf);
        } else {
                if (!(flags & FMT_FLAG_LEFT) && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                                putc_char(' ');
                        }
                }
                if (prefix_len > 0) {
                        puts_len(prefix, prefix_len);
                }
                puts_str(buf);
                if (flags & FMT_FLAG_LEFT && pad_len > 0) {
                        for (int i = 0; i < pad_len; i++) {
                                putc_char(' ');
                        }
                }
        }
}

void log_print(char *buffer, const char *format, va_list arg_list)
{
        (void)buffer;
        const char *p = format;

        while (*p) {
                if (*p != '%') {
                        putc_char(*p++);
                        continue;
                }

                p++; /* skip '%' */

                /* Check for '%%' */
                if (*p == '%') {
                        putc_char('%');
                        p++;
                        continue;
                }

                /* Parse format specifier */
                int flags = 0;
                int width = 0;
                enum length_mod len = LEN_NONE;
                char spec = 0;

                parse_format_spec(&p, &flags, &width, &len, &spec);

                /* Dispatch based on specifier */
                switch (spec) {
                case 'd':
                case 'i': {
                        i64 value;
                        if (len == LEN_L || len == LEN_Z || len == LEN_T
                            || len == LEN_J) {
                                value = va_arg(arg_list, long);
                        } else if (len == LEN_LL) {
                                value = va_arg(arg_list, long long);
                        } else if (len == LEN_H) {
                                value = (short)va_arg(arg_list, int);
                        } else if (len == LEN_HH) {
                                value = (signed char)va_arg(arg_list, int);
                        } else {
                                value = va_arg(arg_list, int);
                        }
                        format_signed(value, 10, 0, flags, width);
                        break;
                }
                case 'u': {
                        u64 value;
                        if (len == LEN_L || len == LEN_Z || len == LEN_T
                            || len == LEN_J) {
                                value = va_arg(arg_list, unsigned long);
                        } else if (len == LEN_LL) {
                                value = va_arg(arg_list, unsigned long long);
                        } else if (len == LEN_H) {
                                value = (unsigned short)va_arg(arg_list,
                                                               unsigned int);
                        } else if (len == LEN_HH) {
                                value = (unsigned char)va_arg(arg_list,
                                                              unsigned int);
                        } else {
                                value = va_arg(arg_list, unsigned int);
                        }
                        format_unsigned(value, 10, 0, flags, width);
                        break;
                }
                case 'x':
                case 'X': {
                        u64 value;
                        if (len == LEN_L || len == LEN_Z || len == LEN_T
                            || len == LEN_J) {
                                value = va_arg(arg_list, unsigned long);
                        } else if (len == LEN_LL) {
                                value = va_arg(arg_list, unsigned long long);
                        } else if (len == LEN_H) {
                                value = (unsigned short)va_arg(arg_list,
                                                               unsigned int);
                        } else if (len == LEN_HH) {
                                value = (unsigned char)va_arg(arg_list,
                                                              unsigned int);
                        } else {
                                value = va_arg(arg_list, unsigned int);
                        }
                        format_unsigned(value, 16, (spec == 'X'), flags, width);
                        break;
                }
                case 'o': {
                        u64 value;
                        if (len == LEN_L || len == LEN_Z || len == LEN_T
                            || len == LEN_J) {
                                value = va_arg(arg_list, unsigned long);
                        } else if (len == LEN_LL) {
                                value = va_arg(arg_list, unsigned long long);
                        } else if (len == LEN_H) {
                                value = (unsigned short)va_arg(arg_list,
                                                               unsigned int);
                        } else if (len == LEN_HH) {
                                value = (unsigned char)va_arg(arg_list,
                                                              unsigned int);
                        } else {
                                value = va_arg(arg_list, unsigned int);
                        }
                        format_unsigned(value, 8, 0, flags, width);
                        break;
                }
                case 'p': {
                        /* Pointer: treat as unsigned long with '#' flag to add
                         * 0x */
                        void *ptr = va_arg(arg_list, void *);
                        int ptr_flags = flags | FMT_FLAG_ALTERNATE;
                        format_unsigned((u64)ptr, 16, 0, ptr_flags, width);
                        break;
                }
                case 'c': {
                        char ch = (char)va_arg(arg_list, int);
                        putc_char(ch);
                        break;
                }
                case 's': {
                        char *str = va_arg(arg_list, char *);
                        if (!str) {
                                str = "(null)";
                        }
                        int len = strlen(str);
                        int pad_len = width > len ? width - len : 0;

                        if (!(flags & FMT_FLAG_LEFT) && pad_len > 0) {
                                for (int i = 0; i < pad_len; i++) {
                                        putc_char(' ');
                                }
                        }

                        puts_str(str);

                        if (flags & FMT_FLAG_LEFT && pad_len > 0) {
                                for (int i = 0; i < pad_len; i++) {
                                        putc_char(' ');
                                }
                        }
                        break;
                }
                default:
                        /* Unknown specifier, output as literal */
                        putc_char('%');
                        putc_char(spec);
                        break;
                }
        }
}

void printk(const char *format, u64 log_level, ...)
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
