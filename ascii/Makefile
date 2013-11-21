CC = clang
CFLAGS = -g -Wextra -O2

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard ../headers/*.h)


.PHONY: default all clean

.PRECIOUS: $(OBJECTS)

default: $(OBJECTS)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) $(LIBS) -c $< -o $@

clean:
	-rm -f $(OBJECTS)
