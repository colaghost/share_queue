#include "sem_lock.h"

#include <sys/sem.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>

int sem_lock_init(sem_lock_t *lock, key_t key)
{
	if (lock == NULL)
		return -2;

	if ((lock->semid = semget(key, 1, S_IRUSR | S_IWUSR | IPC_CREAT | IPC_EXCL)) < 0)
	{
		if (errno == EEXIST)
		{
			lock->semid = semget(key, 1, S_IRUSR | S_IWUSR);
		}

		if (lock->semid < 0)
		{
			perror("semget failed");
			return -1;
		}

	}

	union semun un;
	un.val = 1;
	return semctl(lock->semid, 0, SETVAL, un);
}

int sem_lock_clear(sem_lock_t *lock, int destroy)
{
	if (lock == NULL)
		return -2;

	if (destroy)
		return semctl(lock->semid, 0, IPC_RMID);

	return 0;
}

static int sem_lock_op(sem_lock_t *lock, short op, short flg)
{
	if (lock == NULL)
		return -2;

	struct sembuf opbuf[1];

	opbuf[0].sem_num = 0;
	opbuf[0].sem_op = op;
	opbuf[0].sem_flg = flg;

	return semop(lock->semid, opbuf, 1);
}

int sem_lock_try_acquire(sem_lock_t *lock)
{
	return sem_lock_op(lock, -1, SEM_UNDO | IPC_NOWAIT);
}

int sem_lock_acquire(sem_lock_t *lock)
{
	return sem_lock_op(lock, -1, SEM_UNDO);
}

int sem_lock_release(sem_lock_t *lock)
{
	return sem_lock_op(lock, 1, SEM_UNDO);
}

int sem_lock_wait(sem_lock_t *lock)
{
	if (lock == NULL)
		return -2;

	int ret = 0;
	while (1)
	{
		ret = sem_lock_op(lock, 0, 0);
		if (ret < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			else
			{
				perror("sem_lock_op failed");
				return -1;
			}
		}
		else
		{
			union semun un;
			un.val = 1;
			semctl(lock->semid, 0, SETVAL, un);
			return 0;
		}
	}
	return 0;
}

int sem_lock_notify(sem_lock_t *lock)
{
	if (lock == NULL)
		return -2;

	union semun un;
	int ret = 0;

	un.val = 0;
	while (1)
	{
		ret = semctl(lock->semid, 0, SETVAL, un);
		if (ret < 0)
		{
			if (errno == EINTR)
				continue;
			else
			{
				perror("semctl failed");
				return -1;
			}
		}
		else
		{
			return 0;
		}
	}
	return 0;
}
