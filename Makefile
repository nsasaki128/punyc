CFLAGS=-std=c11 -g -static -fno-common

punyc: main.o
	$(CC) -o $@ $? $(LDFLAGS)

test: punyc
	./test.sh

clean:
	rm -f punyc *.o *~ tmp*

.PHONY: test clean