CFLAGS=-std=c11 -g -static -fno-common
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

punyc: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OBJS): punyc.h

punyc-stage2: punyc $(SRCS) punyc.h self.sh
	./self.sh

test: punyc tests/extern.o
	./punyc tests/tests.c > tmp.s
	gcc -static -o tmp tmp.s tests/extern.o

	./tmp

test-stage2: punyc-stage2 tests/extern.o
	./punyc-stage2 tests/tests.c > tmp.s
	gcc -static -o tmp tmp.s tests/extern.o
	./tmp

clean:
	rm -rf punyc punyc-stage* *.o *~ tmp* tests/*~ tests/*.o

.PHONY: test clean