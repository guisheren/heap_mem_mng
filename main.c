
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include "heap_mem_mng.h"
#define MEM_SIZE (1*1024*1024)

#define TEST_MEM_INIT(ctrl, size)	heap_init(ctrl, size, __FILE__, __LINE__)
#define TEST_MEM_MALLOC(ctrl, size) heap_malloc(ctrl, size, __FILE__, __LINE__)
#define TEST_MEM_FREE(ctrl, buf)	heap_free(ctrl, buf)


void* pthread_test_malloc(void *args)
{
	int size = 0;
	unsigned int cnt = 10;
	int i = 0;

	printf("pthread_run cnt %d\n", cnt);

	while(i++ < cnt)
	{
		size = 16;
		char *p1 = TEST_MEM_MALLOC(args, size);
		memset(p1+16, 0, 4);

		size = rand() % 1024 + 1;
		char *p2 = TEST_MEM_MALLOC(args, size);

		size = rand() % 1024 + 1;
		char *p3 = TEST_MEM_MALLOC(args, size);

		size = rand() % 1024 + 1;
		char *p4 = TEST_MEM_MALLOC(args, size);
		_heap_dump(args);
		//sleep(5);
		TEST_MEM_FREE(args, p1);
		TEST_MEM_FREE(args, p4);
		TEST_MEM_FREE(args, p2);
		TEST_MEM_FREE(args, p3);

		size = rand() % 6 + 1;
		usleep(size * 1000);
	}

}

int main(int argc, char **argv)
{
	void *mem_ctrl = NULL;

	srand((int)time(NULL));

	pthread_t  pit1;
	pthread_t  pit2;

	int ret = TEST_MEM_INIT(&mem_ctrl, MEM_SIZE);
	if(ret != 0)
	{
		printf("error heap_init\n");
		return -1;
	}

	pthread_create(&pit1, NULL, pthread_test_malloc, mem_ctrl);
	pthread_create(&pit2, NULL, pthread_test_malloc, mem_ctrl);

	pthread_join(pit1, NULL);
	pthread_join(pit2, NULL);
	printf("alloc test over!!\n");
	
	printf("heap free size %u\n", _heap_get_free(mem_ctrl));
	_heap_dump(mem_ctrl);

	heap_deinit(mem_ctrl);

	return 0;
}
