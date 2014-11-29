CC=gcc
CFLAGS=-g -Wall
share_queue.a: share_queue.o sem_lock.o
	ar -rv share_queue.a $?
share_queue.o: share_queue.c share_queue.h sem_lock.o
sem_lock.o: sem_lock.c sem_lock.h

clean:
	-rm *.o share_queue.a
