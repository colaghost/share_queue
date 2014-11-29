#include "share_queue.h"
#include <stdio.h>
#define SHM_SIZE 18
int main()
{
	const char *key_path = "/Users/zhanggx/code/share_queue/share_queue.key";
	shm_queue_t queue;
	if (shm_queue_init(&queue, key_path, SHM_SIZE))
	{
		perror("shm_queue_init failed");
		return -1;
	}
	shm_queue_clear(&queue, 1);
	printf("shm_queue_clear success\n");
	return 0;
}
