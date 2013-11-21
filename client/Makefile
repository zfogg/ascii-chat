CC=clang
CFLAGS=-c -Wall

all: client

client: client.o
	$(CC) client.o -o client.out

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

clean:
	rm -rf *.o