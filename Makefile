#!/usr/bin/make -f


BIN_D        = ./bin
OUT_D        = ./build

CC           = g++
CXX          = g++

CFLAGS       = -Wall -O2 -g
CXXFLAGS     = -Wall -O2 -g

CFLAGS      += -D _POSIX_C_SOURCE=200809L
CXXFLAGS    += -D _POSIX_C_SOURCE=200809L

LDFLAGS     += -lncurses -ljpeg
LDFLAGS     += `pkg-config --libs opencv`
LDFLAGS     += -I.

OBJS_C       = $(patsubst %.c,   $(OUT_D)/%.o, $(wildcard *.c))
OBJS_CPP     = $(patsubst %.cpp, $(OUT_D)/%.o, $(wildcard *.cpp))

OBJS         = $(OBJS_C) $(OBJS_CPP)
# OBJS_: not targets, i.e. files without main methods.
OBJS_        = $(filter-out $(patsubst %, $(OUT_D)/%.o, $(TARGETS)), $(OBJS))

HEADERS_C    = $(wildcard *.h)
HEADERS_CPP  = $(wildcard *.hpp)

HEADERS      = $(HEADERS_C) $(HEADERS_CPP)

TARGETS      = server client
TARGETS_     = $(addprefix $(BIN_D)/, $(TARGETS))


.PHONY: clean default all

default: $(TARGETS_)
all: default

.PRECIOUS: $(OBJS)

$(TARGETS_): $(BIN_D)/%: $(OBJS)
	$(CXX) \
		$(CXXFLAGS) \
		$(LDFLAGS) \
		$(OBJS_) \
		$(*F).c -o $@

$(OBJS_C): $(OUT_D)/%.o: %.c $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJS_CPP): $(OUT_D)/%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGETS_)
