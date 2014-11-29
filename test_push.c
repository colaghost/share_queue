#include "share_queue.h"
#include <stdio.h>

#define SHM_SIZE 18
int main()
{
	const char *data = "hello";
	const char *key_path = "/Users/zhanggx/code/share_queue/share_queue.key";
	shm_queue_t queue;
	if (shm_queue_init(&queue, key_path, SHM_SIZE))
	{
		perror("shm_queue_init failed");
		return -1;
	}

	for (size_t i = 0; i < 1; ++i)
	{
		if (shm_queue_push(&queue, data, 5))
		{
			perror("shm_queue_push failed");
			return -1;
		}
		printf("push success\n");
	}
	shm_queue_clear(&queue, 0);
	return 0;
}
