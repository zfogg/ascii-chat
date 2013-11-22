CC = g++
CFLAGS = -g -Wextra -O2

OBJECTS_C = $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS_CPP = $(patsubst %.cpp, %.o, $(wildcard *.cpp))
OBJECTS = $(OBJECTS_C) $(OBJECTS_CPP)
HEADERS = $(wildcard ../headers/*.h)


.PHONY: default all clean

.PRECIOUS: $(OBJECTS)

default: $(OBJECTS)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS)
