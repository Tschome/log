#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define TAG "main"

void main(int argc, int **argv)
{
	log_set_flags(LOG_SKIP_REPEATED);//跳过重复的信息
	//parse_loglevel(argc, argv, options);
	log_set_level(LOG_INFO);
	int i = 0;
//	for(i = 0; i < 10; i++)
		log(TAG, LOG_INFO, "success 0\n");
//	for(i = 0; i < 10; i++)
		log(TAG, LOG_TRACE, "success 1\n");
//	for(i = 0; i < 10; i++)
		log(TAG, LOG_ERROR, "success 2\n");
//	for(i = 0; i < 10; i++)
		log(TAG, LOG_DEBUG, "success 3\n");
//	for(i = 0; i < 10; i++)
		log(TAG, LOG_WARNING, "success 4\n");
		log(TAG, LOG_WARNING, "success 8\n");
	return;
}




