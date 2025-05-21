CC=gcc
CCFLAGS=-O3 -Wall -Wextra -Werror
all: undervolt

undervolt: undervolt.c
	$(CC) $(CCFLAGS) -o undervolt undervolt.c

clean: undervolt
	rm undervolt
