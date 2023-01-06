#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define TAG "main"

void main(int argc, int **argv)
{
	log_set_flags(LOG_SKIP_REPEATED | LOG_PRINT_LEVEL);//跳过重复的信息 + 显示打印级别

	log_set_level(LOG_MAX_OFFSET);
	int i = 0;
	//for(i = 0; i < 10; i++)
	log(TAG, LOG_QUIET, "LOG_QUIET\n");
	log(TAG, LOG_PANIC, "LOG_PANIC\n");
	log(TAG, LOG_FATAL, "LOG_FATAL\n");
	log(TAG, LOG_ERROR, "LOG_ERROR\n");
	log(TAG, LOG_WARNING, "LOG_WARNING\n");
	log(TAG, LOG_INFO, "LOG_INFO\n");
	log(TAG, LOG_VERBOSE, "LOG_VERBOSE\n");
	log(TAG, LOG_DEBUG, "LOG_DEBUG\n");
	log(TAG, LOG_TRACE, "LOG_TRACE\n");
	log(TAG, LOG_MAX_OFFSET, "LOG_MAX_OFFSET\n");
	return;
}




