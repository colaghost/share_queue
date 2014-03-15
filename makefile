CC=gcc
CFLAGS=-g -Wall
all: test_pop test_push test clear_share
clear_share: clear_share.o share_queue.o sem_lock.o
clear_share.o: clear_share.c share_queue.o sem_lock.o
test_pop: test_pop.o share_queue.o sem_lock.o
test_pop.o: test_pop.c share_queue.o sem_lock.o
test_push: test_push.o share_queue.o sem_lock.o
test_push.o: test_push.c share_queue.o sem_lock.o
test: test.o share_queue.o sem_lock.o
test.o: test.c share_queue.o
share_queue.o: share_queue.c share_queue.h sem_lock.o
sem_lock.o: sem_lock.c sem_lock.h

clean:
	-rm *.o test_pop test_push test clear_share
