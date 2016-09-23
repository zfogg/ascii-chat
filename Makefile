#!/usr/bin/make -f


EXT_CDEPS   = fmemopen

BIN_D       = bin
OUT_D       = build

CC          = clang
CXX         = clang++

CFLAGS      = -Wextra -Wno-unused-parameter -O3 -g -std=c99
CXXFLAGS    = -Wextra -Wno-unused-parameter -O3 -g -std=c++11 -stdlib=libc++

CFLAGS     += -DUSE_CLANG_COMPLETER
CXXFLAGS   += -DUSE_CLANG_COMPLETER

CFLAGS     += -isystem /usr/include -I . -x c
CXXFLAGS   += -isystem /usr/include -I . -x c++

LDFLAGS     = -lstdc++ -ljpeg
LDFLAGS    += `pkg-config --libs opencv`

TARGETS     = $(addprefix $(BIN_D)/, server client)

OBJS_C      = $(patsubst %.c,   $(OUT_D)/%.o, $(wildcard *.c))
OBJS_CPP    = $(patsubst %.cpp, $(OUT_D)/%.o, $(wildcard *.cpp))
OBJS_CEXT   = $(patsubst %.c,   $(OUT_D)/%.o, $(wildcard $(addprefix ext/, $(EXT_CDEPS))/*.c))

OBJS        = $(OBJS_C) $(OBJS_CPP) $(OBJS_CEXT)
OBJS_       = $(filter-out $(patsubst $(BIN_D)/%, $(OUT_D)/%.o, $(TARGETS)), $(OBJS))
# ^ non-targets; files without main methods

HEADERS_C    = $(wildcard *.h)
HEADERS_CPP  = $(wildcard *.hpp)
HEADERS_CEXT = $(wildcard $(addprefix ext/, $(EXT_CDEPS))/*.h)

HEADERS      = $(HEADERS_C) $(HEADERS_CPP) $(HEADERS_CEXT)


.PHONY: all clean

default: $(TARGETS)
all: default

.PRECIOUS: $(OBJS)

$(TARGETS): $(BIN_D)/%: $(OUT_D)/%.o $(OBJS_)
	$(CXX) -o $@ $(LDFLAGS) $?

$(OBJS_C): $(OUT_D)/%.o: %.c $(HEADERS)
	$(CC) -c $< -o $@ $(CFLAGS)

$(OBJS_CPP): $(OUT_D)/%.o: %.cpp $(HEADERS)
	$(CXX) -c $< -o $@ $(CXXFLAGS)

$(OBJS_CEXT): $(OUT_D)/%.o: %.c $(HEADERS_CEXT)
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	@echo 'cleaning...'
	@find $(OUT_D) $(BIN_D) -mindepth 1 -not -iname '.gitkeep' -delete -print
	@echo 'done!'
	@echo
