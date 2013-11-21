CC=clang
CFLAGS=-c -Wall

all: server

server: server.o
	$(CC) server.o -o server.out

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

clean:
	rm -rf *.o