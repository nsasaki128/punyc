CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

punyc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): punyc.h

punyc-stage2: punyc $(SRCS) punyc.h self.sh
	./self.sh tmp-stage2 ../punyc punyc-stage2

punyc-stage3: punyc-stage2
	./self.sh tmp-stage3 ../punyc-stage2 punyc-stage3

test: punyc tests/extern.o
	./punyc tests/tests.c > tmp.s
	gcc -static -o tmp tmp.s tests/extern.o

	./tmp

test-stage2: punyc-stage2 tests/extern.o
	./punyc-stage2 tests/tests.c > tmp.s
	gcc -static -o tmp tmp.s tests/extern.o
	./tmp

test-stage3: punyc-stage3
	diff punyc-stage2 punyc-stage3

test-all: test test-stage2 test-stage3

clean:
	rm -rf punyc punyc-stage* *.o *~ tmp* tests/*~ tests/*.o

.PHONY: test clean