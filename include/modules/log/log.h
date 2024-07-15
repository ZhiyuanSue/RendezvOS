#ifndef _SHAMPOOS_LOG_H_
#define _SHAMPOOS_LOG_H_
#include <common/stdarg.h>
#include <common/types.h>
#include <shampoos/list.h>
#define LOG_BUFFER_SIZE 0x10
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
	int log_level;
	int cur_buffer_idx;
	int cur_buffer_offset;
	void (*write_log)(u_int8_t ch);
	struct log_buffer_desc LOG_BUF[LOG_BUFFER_SIZE];
};

void log_init(void *log_buffer_addr, int log_level,
			  void (*write_log)(u_int8_t ch));
void printk(const char *format, int log_level, ...);

#define COLOR_SET(dis_mod, forward_color, backword_color)                      \
	printk("\033[%d;%dm", LOG_OFF, dis_mod, forward_color, backword_color);

#define pr_debug(format, ...)                                                  \
	{                                                                          \
		COLOR_SET(0, 34, 40)                                                   \
		printk(format, LOG_DEBUG, ##__VA_ARGS__);                              \
		COLOR_SET(0, 0, 0)                                                     \
	}

#define pr_info(format, ...)                                                   \
	{                                                                          \
		COLOR_SET(0, 32, 40)                                                   \
		printk(format, LOG_INFO, ##__VA_ARGS__);                               \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_notice(format, ...)                                                 \
	{                                                                          \
		COLOR_SET(0, 33, 40)                                                   \
		printk(format, LOG_NOTICE, ##__VA_ARGS__);                             \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_warn(format, ...)                                                   \
	{                                                                          \
		COLOR_SET(0, 33, 40)                                                   \
		printk(format, LOG_WARNING, ##__VA_ARGS__);                            \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_error(format, ...)                                                  \
	{                                                                          \
		COLOR_SET(0, 31, 40)                                                   \
		printk(format, LOG_ERROR, ##__VA_ARGS__);                              \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_crit(format, ...)                                                   \
	{                                                                          \
		COLOR_SET(0, 35, 40)                                                   \
		printk(format, LOG_CRIT, ##__VA_ARGS__);                               \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_alert(format, ...)                                                  \
	{                                                                          \
		COLOR_SET(0, 35, 40)                                                   \
		printk(format, LOG_ALERT, ##__VA_ARGS__);                              \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define pr_emer(format, ...)                                                   \
	{                                                                          \
		COLOR_SET(0, 35, 40)                                                   \
		printk(format, LOG_EMERG, ##__VA_ARGS__);                              \
		COLOR_SET(0, 0, 0)                                                     \
	}
#define print(format, ...) printk(format, LOG_OFF, ##__VA_ARGS__)

#endif