#ifndef _RENDEZVOS_LOG_H_
#define _RENDEZVOS_LOG_H_
#include <common/stdarg.h>
#include <common/types.h>
#include <common/dsa/list.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>
#include <modules/driver/driver.h>
#include <modules/driver/x86_console/char_console.h>
#include <modules/driver/x86_console/video_console.h>
#define LOG_BUFFER_SIZE        0x10
#define LOG_BUFFER_SINGLE_SIZE 0x1000
enum log_level {
        LOG_OFF,
        LOG_EMERG,
        LOG_ALERT,
        LOG_CRIT,
        LOG_ERROR,
        LOG_WARNING,
        LOG_NOTICE,
        LOG_INFO,
        LOG_DEBUG
};
struct log_buffer_desc {
        void *start_addr;
        u64 length;
};
struct log_buffer {
        u64 log_level;
        u64 cur_buffer_idx;
        u64 cur_buffer_offset;
        struct log_buffer_desc LOG_BUF[LOG_BUFFER_SIZE];
};

void log_init(void *log_buffer_addr, int log_level);
void printk(const char *format, int log_level, ...);

extern struct spin_lock_t log_spin_lock;
extern struct spin_lock_t *log_spin_lock_ptr;

#ifdef SMP
#define COLOR_SET(dis_mod, forward_color, backword_color)     \
        lock_mcs(&log_spin_lock_ptr, &percpu(log_spin_lock)); \
        uart_set_color(dis_mod, forward_color);               \
        SET_CONSOLE_COLOR(&X86_CHAR_CONSOLE,                  \
                          map_color(forward_color, backword_color));

#define COLOR_CLR()                                              \
        SET_CONSOLE_COLOR(&X86_CHAR_CONSOLE, map_color(30, 30)); \
        uart_set_color(0, 0);                                    \
        unlock_mcs(&log_spin_lock_ptr, &percpu(log_spin_lock));
#else
#define COLOR_SET(dis_mod, forward_color, backword_color) \
        uart_set_color(dis_mod, forward_color);           \
        SET_CONSOLE_COLOR(&X86_CHAR_CONSOLE,              \
                          map_color(forward_color, backword_color));

#define COLOR_CLR()                                              \
        SET_CONSOLE_COLOR(&X86_CHAR_CONSOLE, map_color(30, 30)); \
        uart_set_color(0, 0);
#endif

#define pr_debug(format, ...)                             \
        {                                                 \
                COLOR_SET(0, 34, 40)                      \
                printk(format, LOG_DEBUG, ##__VA_ARGS__); \
                COLOR_CLR()                               \
        }

#define pr_info(format, ...)                             \
        {                                                \
                COLOR_SET(0, 32, 40)                     \
                printk(format, LOG_INFO, ##__VA_ARGS__); \
                COLOR_CLR()                              \
        }
#define pr_notice(format, ...)                             \
        {                                                  \
                COLOR_SET(0, 33, 40)                       \
                printk(format, LOG_NOTICE, ##__VA_ARGS__); \
                COLOR_CLR()                                \
        }
#define pr_warn(format, ...)                                \
        {                                                   \
                COLOR_SET(0, 33, 40)                        \
                printk(format, LOG_WARNING, ##__VA_ARGS__); \
                COLOR_CLR()                                 \
        }
#define pr_error(format, ...)                             \
        {                                                 \
                COLOR_SET(0, 31, 40)                      \
                printk(format, LOG_ERROR, ##__VA_ARGS__); \
                COLOR_CLR()                               \
        }
#define pr_crit(format, ...)                             \
        {                                                \
                COLOR_SET(0, 35, 40)                     \
                printk(format, LOG_CRIT, ##__VA_ARGS__); \
                COLOR_CLR()                              \
        }
#define pr_alert(format, ...)                             \
        {                                                 \
                COLOR_SET(0, 35, 40)                      \
                printk(format, LOG_ALERT, ##__VA_ARGS__); \
                COLOR_CLR()                               \
        }
#define pr_emer(format, ...)                              \
        {                                                 \
                COLOR_SET(0, 35, 40)                      \
                printk(format, LOG_EMERG, ##__VA_ARGS__); \
                COLOR_CLR()                               \
        }
#define pr_off(format, ...) \
        {                   \
                ;           \
        }
#define print(format, ...) printk(format, LOG_OFF, ##__VA_ARGS__)

#define rep_print(n, ch)            \
        for (int i = 0; i < n; i++) \
        print("%c", ch)
#endif