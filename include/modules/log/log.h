#ifndef _SHAMPOOS_LOG_H_
#define _SHAMPOOS_LOG_H_
#include <shampoos/types.h>
#include <shampoos/list.h>
#include <shampoos/stdlib.h>
#define LOG_BUFFER_SIZE 0x10

struct log_buffer_desc{

};
enum log_level{

	LOG_ERROR,
	LOG_INFO
};

void init(u64 log_buffer_addr);
void write_log(char* log, int log_level);
#endif