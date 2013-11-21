TARGET = ../bin/server
LIBS =
CC = clang
CFLAGS = -g -Wall -O2

OBJECTS = $(patsubst %.c, ../build/%.o, $(wildcard *.c))
HEADERS = $(wildcard ../headers/*.h)

ALL_OBJECTS = $(wildcard ../build/*.o)


.PHONY: default all clean

.PRECIOUS: $(TARGET) $(OBJECTS)


default: $(TARGET)
all: default

../build/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(ALL_OBJECTS) -Wall $(LIBS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)
