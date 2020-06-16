CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

punyc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): punyc.h

test: punyc
	./punyc tests/tests.c > tmp.s
	echo 'int char_fn() { return 257; } int static_fn() { return 5; }' | \
          gcc -xc -c -o tmp2.o -
	gcc -static -o tmp tmp.s
	./tmp

clean:
	rm -f punyc *.o *~ tmp*

.PHONY: test clean