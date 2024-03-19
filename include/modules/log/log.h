#ifndef _SHAMPOOS_LOG_H_
#define _SHAMPOOS_LOG_H_
#include <shampoos/types.h>
#include <shampoos/list.h>
#include <shampoos/stdlib.h>
#include <shampoos/stdarg.h>
#define LOG_BUFFER_SIZE 0x10
#define LOG_BUFFER_SINGLE_SIZE	0x1000

struct log_buffer_desc{
	void*	start_addr;
	u64		length;
};
struct log_buffer{
	int cur_buffer_idx;
	int cur_buffer_offset;
	void (*write_log)(u_int8_t ch);
	struct log_buffer_desc LOG_BUF[LOG_BUFFER_SIZE];
};

enum log_level{
	LOG_EMERG,
	LOG_ALERT,
	LOG_CRIT,
	LOG_ERROR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG
};

void log_init(void* log_buffer_addr,void (*write_log)(u_int8_t ch));
void printk(const char* format,...);
#endif