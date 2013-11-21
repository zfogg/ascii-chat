CC=clang
CFLAGS=-c -Wall -O2

TARGET = ../bin/server

HEADERS = server.h
OBJECTS = server.o


default: $(TARGET)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f $(TARGET)

