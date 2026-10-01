#ifndef _RENDEZVOS_LOG_H_
#define _RENDEZVOS_LOG_H_
#include <common/stdarg.h>
#include <common/types.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>
#include <modules/driver/uart/uart.h>
#include <modules/driver/x86_char_console/char_console.h>

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

/**
 * @brief Runtime filter: printk emits only when msg_level <= log_level.
 */
extern int log_level;

/**
 * @brief Set @c log_level and print a single newline for test.
 */
void log_init(u64 msg_level);
/**
 * @brief Format according @p format if @p msg_level <= log_level; else no-op.
 *
 * Do not take the log MCS lock
 * Prefer pr_* macros (COLOR_SET holds MCS under SMP).
 */
void printk(const char *format, u64 msg_level, ...);
/**
 * @brief put one byte to the UART console without lock.
 */
void log_put_byte(char ch);
/**
 * @brief Write @p len bytes under the log MCS (SMP).
 * does not change ANSI/VGA color.
 */
void log_put_locked(const u8 *buf, u64 len);

/**
 * @brief log Per-CPU MCS waiter node.
 */
extern struct spin_lock_t log_spin_lock;
/**
 * @brief Global log MCS queue head (shared across CPUs).
 */
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
/**
 * @brief printk with LOG_OFF，no MCS for early print.
 */
#define print(format, ...) printk(format, LOG_OFF, ##__VA_ARGS__)

#define rep_print(n, ch)            \
        for (int i = 0; i < n; i++) \
        print("%c", ch)
#endif