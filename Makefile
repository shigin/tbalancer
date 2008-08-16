all: balancer

#config_test: configl.o configy.o tpool.o
#	gcc -g -Wall -o $@ $^ -levent

balancer: balancer.o tpool.o configl.o configy.o
	gcc -g -Wall -o $@ $^ -levent

.c.o:
	gcc -g -Wall -c $< 

.l.c:
	LANG=C flex -o $@ $<

.y.c:
	bison -d -o $@ $<

balancer.o: tpool.h common.h
tpool.o: tpool.h common.h
configy.h: configy.c
configl.o: configy.h
