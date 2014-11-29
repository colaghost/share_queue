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

	assert(queue && path);

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

	queue_info_t *info = queue->queue_info;

	//初始化
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

	if (!queue_exists)
	{
		info->push_pos = 0;
		info->pop_pos = 0;
		info->left_size = size;
		info->size = size;
	}

	return 0;

ERR_PROCESS:
	shm_queue_clear(queue, 0);
	return -1;
}

void shm_queue_clear(shm_queue_t *queue, int destroy)
{
	assert(queue);

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

	char *len_write_start = NULL;
	char *data_write_start = NULL;
	unsigned int update_push_pos = 0;
	unsigned int update_left_size = info->left_size;

	if (update_left_size < len + sizeof(len))
		return 1;

	/* -可用 *数据
	 * |queue_head_left|********|queue_tail_left|
	 * |---------------pop***push---------------| 这种情况
	 */
	if (info->push_pos >= info->pop_pos)
	{
		unsigned int queue_tail_left = info->size - info->push_pos;
		unsigned int queue_head_left = info->pop_pos;

		//尾部剩余空间足够放长度信息和整个数据
		if (queue_tail_left >= sizeof(len) + len)
		{
			len_write_start = queue->start + info->push_pos;
			data_write_start = len_write_start + sizeof(len);

			update_push_pos = info->push_pos + len + sizeof(len);
			update_left_size -= (len + sizeof(len));
		}
		//尾部剩余空间仅够放长度信息，但是头部剩余空间足够放整个数据
		else if (queue_head_left >= sizeof(len) && queue_tail_left >= len)
		{
			len_write_start = queue->start + info->push_pos;
			data_write_start = queue->start;

			update_push_pos = len;
			update_left_size -= (queue_tail_left + len);
		}
		//尾部剩余空间不够放长度信息，但头部剩余空间足够放长度信息和整个数据
		else if (queue_head_left >= len + sizeof(len))
		{
			len_write_start = queue->start;
			data_write_start = queue->start + sizeof(len);

			update_push_pos = len + sizeof(len);
			update_left_size -= (queue_tail_left + len + sizeof(len));
		}
		//没有足够空间
		else
		{
			return 1;
		}
	}
	/* |*************push---------pop**********|
	 */
	else
	{
		unsigned int left_len = info->pop_pos - info->push_pos;
		if (left_len >= sizeof(len) + len)
		{
			len_write_start = queue->start + info->push_pos;
			data_write_start = len_write_start + sizeof(len);

			update_push_pos = info->push_pos + len + sizeof(len);
			update_left_size -= (len + sizeof(len));
		}
		else
		{
			return 1;
		}
	}

	memcpy(len_write_start, (const void*)&len, sizeof(len));
	memcpy(data_write_start, buf, len);

	//修正
	info->push_pos = update_push_pos >= info->size ? 0 : update_push_pos;
	info->left_size = update_left_size;

	return 0;
}

int shm_queue_push(shm_queue_t *queue, const void *buf, unsigned len)
{
	int ret = 0;
	assert(queue && buf && (len != 0));

	while (1)
	{
		if (sem_lock_acquire(queue->queue_lock))
		{
			return -1;
		}
		if (shm_queue_left_size(queue) < len + sizeof(len))
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
	unsigned int data_len = 0;
	queue_info_t *info = queue->queue_info;
	unsigned int update_pop_pos = info->pop_pos;
	unsigned int update_left_size = info->left_size;

	//没有数据
	if (update_left_size == info->size)
	{
		return 1;
	}

	//剩下的空间不足放长度信息，那就跳过吧
	if (update_pop_pos >= info->push_pos && 
			update_pop_pos + sizeof(*len) > info->size)
	{
		update_pop_pos = 0;
		update_left_size += (info->size - info->pop_pos);
	}

	data_len = *(unsigned int*)(queue->start + update_pop_pos);
	if (data_len > *len)
	{
		return data_len;
	}

	*len = data_len;

	char *data_start = queue->start + update_pop_pos + sizeof(*len);
	if (update_pop_pos >= info->push_pos &&
			info->size - update_pop_pos - sizeof(*len) < data_len)
	{
		data_start = queue->start;
		update_pop_pos = data_len;
		update_left_size += (info->size - update_pop_pos + data_len);
	}
	else
	{
		update_pop_pos += data_len + sizeof(data_len);
		update_left_size += data_len + sizeof(data_len);
	}

	memcpy(buf, (const void*)data_start, data_len);

	//修正
	info->pop_pos = update_pop_pos >= info->size ? 0 : update_pop_pos;
	info->left_size = update_left_size;

	return 0;
}

int shm_queue_pop(shm_queue_t *queue, void *buf, unsigned *len)
{
	int ret = 0;
	assert(queue && buf && len);

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
	assert(queue);
	return queue->queue_info->size;
}

unsigned int shm_queue_left_size(shm_queue_t *queue)
{
	assert(queue);
	//可能需要做修正
	queue_info_t *info = queue->queue_info;
	if (info->push_pos == info->pop_pos)
	{
		//发生这种情况只能丢掉数据了
		if (info->left_size != info->size && info->left_size != 0)
		{
			info->left_size = info->size;
		}
	}
	else if (info->push_pos > info->pop_pos)
	{
		info->left_size = info->size - info->push_pos + info->pop_pos;
	}
	else
	{
		info->left_size = info->pop_pos - info->push_pos;
	}
	return queue->queue_info->left_size;
}

int shm_queue_empty(shm_queue_t *queue)
{
	if (queue == NULL)
		return 1;
	return (shm_queue_left_size(queue) == shm_queue_size(queue));
}

void shm_queue_stat(shm_queue_t *queue)
{
	if (queue == NULL)
		return;
	printf("queue_size:       %u\n", queue->queue_info->size);
	printf("queue_left_size:  %u\n", queue->queue_info->left_size);
	printf("queue_push_pos:   %u\n", queue->queue_info->push_pos);
	printf("queue_pop_pos:    %u\n", queue->queue_info->pop_pos);
	printf("====================\n");
}
