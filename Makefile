all: balancer
CFLAGS=-g -Wall -I/usr/local/include
LDFLAGS=-L/usr/local/lib

# pmake and gmake extend POSIX make in a different ways
# pmake uses $> to store all source files
# gmake uses $^ instead

#config_test: configl.o configy.o tpool.o
#	$(CC) -o $@ $^ -levent

balancer: balancer.o tpool.o configl.o configy.o
	$(CC) $(LDFLAGS) -o $@ $^ $> -levent

configl.o: configl.c

.l.c:
	LANG=C flex -o$@ $<

.y.c:
	bison -d -o $@ $<

balancer.o: tpool.h common.h
tpool.o: tpool.h common.h
configy.h: configy.c
configl.o: configy.h

clean:
	rm -f hash_test balancer balancer.o tpool.o
	rm -f configl.o configl.c
	rm -f configy.o configy.h configy.c 
