#include "share_queue.h"
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define SHM_SIZE 11

int main()
{
	int i = 0;
	//key_t key = 0;
	const char *data = "hello";
	char buf[1024] = {0};
	unsigned int len = 1024;
	shm_queue_t queue;

	//key = ftok("/Users/zhanggx/code/share_queue/share_queue.key", 0);

	if (shm_queue_init(&queue, "/Users/zhanggx/code/share_queue/test/share_queue.key", SHM_SIZE))
	{
		perror("shm_queue_init failed");
		return -1;
	}

	for (; i < 2; ++i)
	{
		len = 1024;
		memset(buf, 0, 1024);
		if (shm_queue_push(&queue, data, 5) == 0)
		{
			shm_queue_stat(&queue);
			shm_queue_pop(&queue, buf, &len);
			shm_queue_stat(&queue);
			printf("%d:%s\n", len, buf);
		}
	}


	shm_queue_clear(&queue, 1);
	return 0;
}
