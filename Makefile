#!/usr/bin/make -f

CC              := clang
CXX             := clang++
PKG_CONFIG_LIBS := opencv libjpeg

BIN_D = bin
OUT_D = build

CFLAGS     += -DUSE_CLANG_COMPLETER -Wall -Wextra #-Wl,--no-as-needed
CXXFLAGS   += $(CFLAGS)

PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
CFLAGS     += $(filter-out --std=c99,   ${PKG_CFLAGS})
CXXFLAGS   += $(filter-out --std-c++11, ${PKG_CFLAGS})
CFLAGS     += -x c   -std=c11
CXXFLAGS   += -x c++ -std=c++11 -stdlib=libc++

LDFLAGS    += -lstdc++
LDFLAGS    += $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

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

# util
RM = /bin/rm


.DEFAULT: default
.PHONY: all clean

.PRECIOUS: $(OBJS_)


default: $(TARGETS)

all: default


$(TARGETS): $(BIN_D)/%: $(OUT_D)/%.o $(OBJS_)
	$(CXX) -o $@ \
	$(LDFLAGS) \
	$?

$(OBJS_C): $(OUT_D)/%.o: %.c $(HEADERS)
	$(CC) -c $< -o $@ \
	$(CFLAGS)

$(OBJS_CPP): $(OUT_D)/%.o: %.cpp $(HEADERS)
	$(CXX) -c $< -o $@ \
	$(CXXFLAGS)

$(OBJS_CEXT): $(OUT_D)/%.o: %.c $(HEADERS_CEXT)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ \
	$(CFLAGS)

clean:
	@echo 'cleaning...'
	@printf '\t'
	@find $(OUT_D) -mindepth 1 \
		-type f -not -iname '.gitkeep' \
		-printf '%P ' -delete
	@find $(BIN_D) -mindepth 1 \
		-type f -not -iname '.gitkeep' \
		-printf '%P ' -delete
	@printf '\n'
	@echo 'done!'
