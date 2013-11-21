TARGET = ../bin/server
LIBS = -ljpeg -lncurses
CC = clang
CFLAGS = -g -Wextra -O2

OBJS = $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS = $(filter-out server.o, $(OBJS))
NODEP_OBJS = $(patsubst %.c, %.o, $(wildcard ../ascii/*.c))
HEADERS = $(wildcard ../headers/*.h)


.PHONY: default all clean

.PRECIOUS: $(TARGET) $(OBJECTS)


default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(NODEP_OBJS) -Wall $(LIBS) server.c -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)
