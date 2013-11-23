#!/usr/bin/make -f


BIN_D     = ./bin

CC        = g++
CXX       = g++

CFLAGS    = -Wall -O2 -g
CXXFLAGS  = -Wall -O2 -g

CFLAGS   += -D _POSIX_C_SOURCE=200809L
CXXFLAGS += -D _POSIX_C_SOURCE=200809L

LDFLAGS  += -lncurses -ljpeg
LDFLAGS  += `pkg-config --libs opencv`
LDFLAGS  += -I.

OBJS_C    = $(patsubst %.c, %.o, $(wildcard *.c))
OBJS_CPP  = $(patsubst %.cpp, %.o, $(wildcard *.cpp))

OBJS      = $(OBJS_C) $(OBJS_CPP)
OBJS_     = $(filter-out $(addsuffix .o, $(TARGETS)), $(OBJS))

HEADERS   = $(wildcard *.h)

TARGETS   = server client
TARGETS_  = $(addprefix $(BIN_D)/, $(TARGETS))


.PHONY: clean default all

default: $(TARGETS_)
all: default

.PRECIOUS: $(OBJS)

$(TARGETS_): $(BIN_D)/%: $(OBJS) $(HEADERS)
	$(CXX) \
		$(CXXFLAGS) \
		$(LDFLAGS) \
		$(OBJS_) \
		$(*F).c -o $@

clean:
	rm -f $(OBJS) $(TARGETS_)
