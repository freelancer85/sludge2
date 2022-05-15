CC=gcc
CFLAGS=-Wall -Werror -pedantic -ggdb
BIN=sludge

sludge: sludge.c
	$(CC) $(CFLAGS) -o $(BIN) sludge.c

clean:
	rm $(BIN)
