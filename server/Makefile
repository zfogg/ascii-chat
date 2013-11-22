TARGET = ../bin/server
TARGET_SRC = server
LIBS = -ljpeg -lncurses `pkg-config --libs opencv`
CC = g++
CFLAGS = -g -Wextra -O2

OBJS = $(patsubst %.c, %.o, $(wildcard *.c))
OBJECTS = $(filter-out server.o, $(OBJS))
NODEP_OBJS = $(patsubst %.c, %.o, $(wildcard ../ascii/*.c))
NODEP_OBJS_CPP = $(patsubst %.cpp, %.o, $(wildcard ../ascii/*.cpp))
HEADERS = $(wildcard ../headers/*.h)


.PHONY: default all clean

.PRECIOUS: $(TARGET) $(OBJECTS)


default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(NODEP_OBJS) $(NODEP_OBJS_CPP) -Wall $(LIBS) $(TARGET_SRC).c -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)
