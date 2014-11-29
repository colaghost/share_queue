#ifndef _SHARE_QUEUE_H
#define _SHARE_QUEUE_H

#include <sys/ipc.h>

#include "sem_lock.h"
typedef struct queue_info
{
	unsigned int push_pos;
	unsigned int pop_pos;
	unsigned int left_size;
	unsigned int size;
}queue_info_t;

/* |queue_info|len|data|len|data| */
typedef struct shm_queue
{
	queue_info_t *queue_info;
	sem_lock_t *queue_lock;
	sem_lock_t *empty_lock;
	sem_lock_t *full_lock;
	char *start;
	int shmid;
}shm_queue_t;


int shm_queue_init(shm_queue_t *queue, const char *key_path, unsigned int size);
void shm_queue_clear(shm_queue_t *queue, int destroy);
//queue_info->left_size并不一直是对的，所以每次调用此函数时
//都需要做修正
unsigned int shm_queue_left_size(shm_queue_t *queue);
unsigned int shm_queue_size(shm_queue_t *queue);
int shm_queue_empty(shm_queue_t *queue);

int shm_queue_push(shm_queue_t *queue, const void *buf, unsigned len);
int shm_queue_pop(shm_queue_t *queue, void *buf, unsigned *len);

void shm_queue_stat(shm_queue_t *queue);

#endif
