#include "share_queue.h"
#include <stdio.h>

#define SHM_SIZE 9
int main()
{
	char buf[1024] = {0};
	unsigned int len = 1024;
	const char *key_path = "/Users/zhanggx/code/share_queue/test/share_queue.key";
	shm_queue_t queue;
	if (shm_queue_init(&queue, key_path, SHM_SIZE))
	{
		perror("shm_queue_init failed");
		return -1;
	}

	if (shm_queue_pop(&queue, buf, &len))
	{
		perror("shm_queue_pop failed");
		return -1;
	}
	printf("%u:%s\n", len, buf);
	shm_queue_clear(&queue, 0);
	return 0;
}
