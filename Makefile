#!/usr/bin/make -f


BIN_D       = bin
OUT_D       = build

CC          = clang -std=c99
CXX         = clang

CFLAGS      = -Wextra -pedantic -O2 -g
CXXFLAGS    = -Wextra -pedantic -O2 -g

CFLAGS     += -D_POSIX_C_SOURCE=200809L
CXXFLAGS   +=

LDFLAGS    += -lstdc++ -lncurses -ljpeg
LDFLAGS    += `pkg-config --libs opencv`
LDFLAGS    += -I.

TARGETS     = $(addprefix $(BIN_D)/, server client)

OBJS_C      = $(patsubst %.c,   $(OUT_D)/%.o, $(wildcard *.c))
OBJS_CPP    = $(patsubst %.cpp, $(OUT_D)/%.o, $(wildcard *.cpp))

OBJS        = $(OBJS_C) $(OBJS_CPP)
OBJS_       = $(filter-out $(patsubst $(BIN_D)/%, $(OUT_D)/%.o, $(TARGETS)), $(OBJS))
# ^ non-targets; files without main methods

HEADERS_C   = $(wildcard *.h)
HEADERS_CPP = $(wildcard *.hpp)
HEADERS     = $(HEADERS_C) $(HEADERS_CPP)


.PHONY: clean default all

default: $(TARGETS)
all: default

.PRECIOUS: $(OBJS)

$(TARGETS): $(BIN_D)/%: $(OUT_D)/%.o $(OBJS_)
	$(CXX) \
		-o $@  \
		$(LDFLAGS) \
		$?

$(OBJS_C): $(OUT_D)/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJS_CPP): $(OUT_D)/%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGETS)
