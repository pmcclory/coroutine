CFLAGS := -Wall -Wextra -Werror
ifeq ($(DEBUG), 1)
CFLAGS += -O0 -ggdb -gdwarf-2 -g3
endif

all: coroutine.o test

coroutine.o: coroutine.c
	$(CC) $(CFLAGS) -c -o $@ $^

test: test.c coroutine.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf coroutine.o test
