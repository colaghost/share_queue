#ifndef _SEM_LOCK_H
#define _SEM_LOCK_H

#include <sys/ipc.h>
typedef struct sem_lock
{
	int semid;
}sem_lock_t;

int sem_lock_init(sem_lock_t *lock, key_t key);
int sem_lock_clear(sem_lock_t *lock, int destroy);

int sem_lock_try_acquire(sem_lock_t *lock);
int sem_lock_acquire(sem_lock_t *lock);
int sem_lock_release(sem_lock_t *lock);

int sem_lock_wait(sem_lock_t *lock);
int sem_lock_notify(sem_lock_t *lock);
#endif
