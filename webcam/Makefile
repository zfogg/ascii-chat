TARGET   := webcam.o
CC       := g++
CPPFLAGS := -g -Wextra -O2 -libstdc++

OBJECTS := $(filter-out client.o, $(patsubst %.c, %.o, $(wildcard *.c)))
HEADERS := $(patsubst %.c, ../headers/%.h, $(wildcard *.c))


.PHONY: default all clean

.PRECIOUS: $(TARGET) $(OBJECTS)


default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CPPFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)
