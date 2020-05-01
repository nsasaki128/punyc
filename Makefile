CFLAGS=-std=c11 -g -static -fno-common

tinyc: main.o
	$(CC) -o $@ $? $(LDFLAGS)

test: tinyc
	./test.sh

clean:
	rm -f tinyc *.o *~ tmp*

.PHONY: test clean