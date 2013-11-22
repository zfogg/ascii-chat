TARGET   := webcam.o
CC       := g++
CPPFLAGS := -g -Wextra -O2 -libstdc++

OBJECTS := $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS := $(patsubst %.c, %.h, $(wildcard *.c))


.PHONY: default all clean

.PRECIOUS: $(TARGET) $(OBJECTS)


default: $(TARGET)
all: default

%.o: %.c $(HEADERS)
	$(CC) $(CPPFLAGS) -c $< -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)
