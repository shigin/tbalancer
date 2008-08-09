all: balancer

balancer: balancer.o tpool.o
	gcc -g -Wall -o $@ $^ -levent

.c.o:
	gcc -g -Wall -c $< 
