#include "share_queue.h"

#include <sys/ipc.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

int shm_queue_init(shm_queue_t *queue, const char *path, unsigned int size)
{
	char *shmptr = NULL;
	int queue_exists = 0;
	key_t queue_key, queue_lock_key, empty_lock_key, full_lock_key;

	if (queue == NULL || path == NULL)
		return -2;

	queue_key = ftok(path, 0);
	queue_lock_key = ftok(path, 1);
	empty_lock_key = ftok(path, 2);
	full_lock_key = ftok(path, 3);

	memset(queue, 0, sizeof(shm_queue_t));
	if ((queue->shmid = shmget(queue_key, sizeof(queue_info_t) + size, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL)) < 0)
	{
		if (errno == EEXIST)
		{
			queue->shmid = shmget(queue_key, 0, S_IRUSR | S_IWUSR);
			queue_exists = 1;
		}

		if (queue->shmid < 0)
		{
			perror("shmget failed");
			return -1;
		}
	}


	if ((shmptr = shmat(queue->shmid, 0, 0)) == (void*)-1)
	{
		shmctl(queue->shmid, IPC_RMID, 0);
		perror("shmat failed");
		return -1;
	}

	queue->queue_info = (queue_info_t*)shmptr;
	queue->start = (char*)shmptr + sizeof(queue_info_t);
	if (!queue_exists)
	{
		queue->queue_info->push_pos = queue->queue_info->pop_pos = 0;
		queue->queue_info->size = size;
	}

	queue->queue_lock = (sem_lock_t*)calloc(1, sizeof(sem_lock_t));
	queue->empty_lock = (sem_lock_t*)calloc(1, sizeof(sem_lock_t));
	queue->full_lock = (sem_lock_t*)calloc(1, sizeof(sem_lock_t));

	if (sem_lock_init(queue->queue_lock, queue_lock_key) || 
			sem_lock_init(queue->empty_lock, empty_lock_key) ||
			sem_lock_init(queue->full_lock, full_lock_key))
	{
		perror("sem_lock_init failed");
		goto ERR_PROCESS;
	}

	return 0;

ERR_PROCESS:
	shm_queue_clear(queue, 0);
	return -1;
}

void shm_queue_clear(shm_queue_t *queue, int destroy)
{
	if (queue == NULL)
		return;

	shmdt((void*)queue->queue_info);
	if (destroy)
	{
		shmctl(queue->shmid, IPC_RMID, 0);
	}

	if (queue->queue_lock)
	{
		sem_lock_clear(queue->queue_lock, destroy);
		free(queue->queue_lock);
	}
	if (queue->empty_lock)
	{
		sem_lock_clear(queue->empty_lock, destroy);
		free(queue->empty_lock);
	}
	if (queue->full_lock)
	{
		sem_lock_clear(queue->full_lock, destroy);
		free(queue->full_lock);
	}

	memset(queue, 0, sizeof(shm_queue_t));
}

static int shm_queue_internal_push(shm_queue_t *queue, const void *buf, unsigned len)
{
	queue_info_t *info = queue->queue_info;
	printf("%s:%u:%u\n", __func__, info->push_pos, info->pop_pos);

	if (info->push_pos >= info->pop_pos)
	{
		unsigned int queue_tail_left = info->size - info->push_pos;
		unsigned int queue_head_left = info->pop_pos;
		if (queue_tail_left >= (sizeof(len) + len))
		{
			memcpy(queue->start + info->push_pos, (const void*)&len, sizeof(len));
			memcpy(queue->start + info->push_pos + sizeof(len), buf, len);
			info->push_pos += (len + sizeof(len));
			return 0;
		}
		else if (queue_tail_left >= sizeof(len))
		{
			if (queue_head_left >= (len - (queue_tail_left - sizeof(len))))
			{
				unsigned left_len = len - (queue_tail_left - sizeof(len));
				memcpy(queue->start + info->push_pos, (const void*)&(len), sizeof(len));
				memcpy(queue->start + info->push_pos + sizeof(len), buf, queue_tail_left - sizeof(len));
				memcpy(queue->start, buf + queue_tail_left - sizeof(len), left_len);
				info->push_pos = left_len;
				return 0;
			}
		}
		else if (queue_head_left >= (sizeof(len) + len))
		{
			memcpy(queue->start, (const void*)&(len), sizeof(len));
			memcpy(queue->start + sizeof(len), buf, len);
			info->push_pos = len + sizeof(len);
			return 0;
		}
	}
	else
	{
		unsigned left_len = info->pop_pos - info->push_pos;
		if (left_len >= sizeof(len) + len)
		{
			memcpy(queue->start + info->push_pos, (const void*)&(len), sizeof(len));
			memcpy(queue->start + info->push_pos + sizeof(len), buf, len);
			info->push_pos += (len + sizeof(len));
			return 0;
		}
	}
	return 1;
}

int shm_queue_push(shm_queue_t *queue, const void *buf, unsigned len)
{
	int ret = 0;
	if (queue == NULL || 
			buf == NULL || 
			len == 0 || 
			len > queue->queue_info->size - sizeof(len))
		return -2;

	while (1)
	{
		if (sem_lock_acquire(queue->queue_lock))
		{
			return -1;
		}
		if (shm_queue_size(queue) < len + sizeof(len))
		{
			if (sem_lock_release(queue->queue_lock))
				return -1;
			if (sem_lock_wait(queue->full_lock))
				return -1;
			continue;
		}
		ret = shm_queue_internal_push(queue, buf, len);
		sem_lock_release(queue->queue_lock);
		sem_lock_notify(queue->empty_lock);
		break;
	}
	return ret;
}

static int shm_queue_internal_pop(shm_queue_t *queue, void *buf, unsigned *len)
{
	unsigned data_len = 0;
	queue_info_t *info = queue->queue_info;
	printf("%s:%u:%u\n", __func__, info->push_pos, info->pop_pos);
	if (info->pop_pos > info->push_pos && 
			info->pop_pos + sizeof(*len) > info->size)
		info->pop_pos = 0;

	if (info->push_pos > info->pop_pos)
	{
		data_len = *(unsigned int*)(queue->start + info->pop_pos);
		if (data_len > *len)
			return data_len;
		*len = data_len;
		memcpy(buf, (const void*)(queue->start + info->pop_pos + sizeof(*len)), data_len);
		info->pop_pos += data_len + sizeof(*len);
		return 0;
	}
	else if (info->pop_pos > info->push_pos)
	{
		unsigned left_len = 0;
		left_len = info->size - info->pop_pos - sizeof(*len);
		data_len = *(unsigned int*)(queue->start + info->pop_pos);
		if (data_len > *len)
			return data_len;
		*len = data_len;
		if (left_len >= data_len)
		{
			memcpy(buf, queue->start + info->pop_pos + sizeof(*len), data_len);
			info->pop_pos += data_len + sizeof(*len);
		}
		else
		{
			memcpy(buf, queue->start + info->pop_pos + sizeof(*len), left_len);
			memcpy(buf + left_len, queue->start, data_len - left_len);
			info->pop_pos = data_len - left_len;
		}
		return 0;
	}
	return 1;
}

int shm_queue_pop(shm_queue_t *queue, void *buf, unsigned *len)
{
	int ret = 0;
	if (queue == NULL || buf == NULL || len == NULL)
		return -1;

	while (1)
	{
		if (sem_lock_acquire(queue->queue_lock))
			return -1;
		if (shm_queue_empty(queue))
		{
			if (sem_lock_release(queue->queue_lock))
				return -1;
			if (sem_lock_wait(queue->empty_lock))
				return -1;
			continue;
		}

		ret = shm_queue_internal_pop(queue, buf, len);
		sem_lock_release(queue->queue_lock);
		sem_lock_notify(queue->full_lock);
		break;
	}
	return ret;
}

unsigned int shm_queue_size(shm_queue_t *queue)
{
	if (queue == NULL)
		return 0;

	queue_info_t *info = queue->queue_info;
	if (info->push_pos >= info->pop_pos)
	{
		return (info->size - info->push_pos) + info->pop_pos;
	}
	else
	{
		return (info->pop_pos - info->push_pos);
	}
}

int shm_queue_empty(shm_queue_t *queue)
{
	if (queue == NULL)
		return 1;
	return (queue->queue_info->push_pos == queue->queue_info->pop_pos);
}
