CC = clang
CFLAGS = -g -Wall -O2

OBJECTS = $(patsubst %.c, ../build/%.o, $(wildcard *.c))
HEADERS = $(wildcard ../headers/*.h)


.PHONY: default all clean

.PRECIOUS: $(OBJECTS)

default: $(OBJECTS)
all: default

../build/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS)
