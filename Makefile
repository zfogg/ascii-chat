#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
CC  := clang
CXX := clang++

# Package dependencies
PKG_CONFIG_LIBS := opencv4 libjpeg zlib portaudio-2.0

# Directories
BIN_DIR  := bin
BUILD_DIR := build

# =============================================================================
# Compiler Flags
# =============================================================================

CSTD := -std=c23
CXXSTD := -std=c++23

# Base flags
BASE_FLAGS := -Wall -Wextra

# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
FEATURE_FLAGS := -D_GNU_SOURCE

CFLAGS   += $(BASE_FLAGS) $(FEATURE_FLAGS)
CXXFLAGS += $(BASE_FLAGS) $(FEATURE_FLAGS)

# Get package-specific flags
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
PKG_LDFLAGS := $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

CFLAGS   +=  $(PKG_CFLAGS) $(CSTD)
CXXFLAGS +=  $(PKG_CFLAGS) $(CXXSTD)

LDFLAGS := $(PKG_LDFLAGS) -lpthread

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

C_FILES := $(wildcard *.c) 
CPP_FILES := $(wildcard *.cpp) 

# Object files for server and client
OBJS_C    := $(patsubst %.c,   $(BUILD_DIR)/%.o, $(wildcard *.c))
OBJS_CPP  := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(wildcard *.cpp))

# All object files for server and client
OBJS := $(OBJS_C) $(OBJS_CPP)

# Non-target object files (files without main methods)
OBJS_NON_TARGET := $(filter-out $(patsubst $(BIN_DIR)/%, $(BUILD_DIR)/%.o, $(TARGETS)), $(OBJS))

# Header files
HEADERS_C    := $(wildcard *.h)
HEADERS_CPP  := $(wildcard *.hpp)
HEADERS      := $(HEADERS_C) $(HEADERS_CPP)

# =============================================================================
# Phony Targets
# =============================================================================

.PHONY: all clean default help debug release format format-check bear clang-tidy

# =============================================================================
# Default Target
# =============================================================================

.DEFAULT_GOAL := default

# =============================================================================
# Build Rules
# =============================================================================

# Main targets
default: $(TARGETS)
all: default

# Debug build
debug: CFLAGS += -g -O0
debug: CXXFLAGS += -g -O0
debug: $(TARGETS)

# Release build
release: CFLAGS += -O3
release: CXXFLAGS += -O3
release: $(TARGETS)

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/server.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/client.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

# Compile C source files
$(OBJS_C): $(BUILD_DIR)/%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile C++ source files
$(OBJS_CPP): $(BUILD_DIR)/%.o: %.cpp $(HEADERS)
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(CXXFLAGS) -c $<

# =============================================================================
# Utility Targets
# =============================================================================

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@if [ -d "$(BUILD_DIR)" ]; then \
		find $(BUILD_DIR) -mindepth 1 -type f -not -iname '.gitkeep' -delete; \
		echo "  - Removed build files"; \
	fi
	@if [ -d "$(BIN_DIR)" ]; then \
		find $(BIN_DIR) -mindepth 1 -type f -not -iname '.gitkeep' -delete; \
		echo "  - Removed binaries"; \
	fi
	@rm -f compile_commands.json
	@echo "  - Removed compile_commands.json"
	@echo "Clean complete!"

# Show help information
help:
	@echo "Available targets:"
	@echo "  all/default - Build all targets with default flags"
	@echo "  debug       - Build with debug symbols and no optimization"
	@echo "  release     - Build with optimizations enabled"
	@echo "  format      - Format source code using clang-format"
	@echo "  format-check- Check code formatting without modifying files"
	@echo "  clean       - Remove build artifacts"
	@echo "  help        - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  CC=$(CC)"
	@echo "  CXX=$(CXX)"
	@echo "  BIN_DIR=$(BIN_DIR)"
	@echo "  BUILD_DIR=$(BUILD_DIR)"
	@echo "  PKG_CONFIG_LIBS=$(PKG_CONFIG_LIBS)"
	@echo ""
	@echo "New files included:"
	@echo "  - common.h, logging.c       - Logging and error handling"
	@echo "  - ringbuffer.h, ringbuffer.c - Lock-free frame buffering"
	@echo "  - protocol.h                - Protocol definitions"
	@echo "  - network.c, network.h      - Network timeouts and utilities"

# =============================================================================
# Code Formatting
# =============================================================================

# Format source code
format:
	@echo "Formatting source code..."
	@find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format -i; \
	echo "Code formatting complete!"

# Check code formatting
format-check:
	@echo "Checking code formatting..."
	find . -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format --dry-run --Werror

# Run bear to generate a compile_commands.json file
compile_commands.json: Makefile
	@echo "Running bear to generate compile_commands.json..."
	@bear -- make clean debug
	@echo "Bear complete!"

# Run clang-tidy to check code style
clang-tidy: $(wildcard *.c) $(wildcard *.cpp) $(wildcard *.h) $(wildcard *.hpp)
	@#clang-tidy -header-filter='.*' $^ -- $(BASE_FLAGS) $(FEATURE_FLAGS) $(PKG_CFLAGS)
	@clang-tidy $(wildcard *.cpp) $(wildcard *.hpp) -- $(CXXFLAGS)
	@clang-tidy $(wildcard *.c)   $(wildcard *.h)   -- $(CFLAGS)


# =============================================================================
# Dependencies
# =============================================================================

.PRECIOUS: $(OBJS_NON_TARGET)
