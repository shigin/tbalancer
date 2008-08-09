all: balancer

balancer: balancer.o tpool.o
	gcc -g -Wall -o $@ $^ -levent

.c.o:
	gcc -g -Wall -c $< 

balancer.o: tpool.h
tpool.o: tpool.h
