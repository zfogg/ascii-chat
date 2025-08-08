#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
override CC  := clang

# Package dependencies
PKG_CONFIG_LIBS := zlib portaudio-2.0

# Directories
BIN_DIR  := bin
BUILD_DIR := build

# =============================================================================
# Compiler Flags
# =============================================================================

CSTD := c23
CXXSTD := c++23

# Base flags
BASE_FLAGS := -Wall -Wextra

# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
FEATURE_FLAGS := -D_GNU_SOURCE

override CFLAGS   += $(BASE_FLAGS) $(FEATURE_FLAGS)
override CXXFLAGS += $(BASE_FLAGS) $(FEATURE_FLAGS)

# Get package-specific flags
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
PKG_LDFLAGS := $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

override CFLAGS   += $(PKG_CFLAGS) -std=$(CSTD)
override CXXFLAGS += $(PKG_CFLAGS) -std=$(CXXSTD)

# Platform-specific flags
ifeq ($(shell uname),Darwin)
    # macOS: Add AVFoundation and CoreMedia frameworks
    PLATFORM_LDFLAGS := -framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
    PLATFORM_SOURCES := webcam_avfoundation.m
else ifeq ($(shell uname),Linux)
    # Linux: No additional frameworks needed for V4L2
    PLATFORM_LDFLAGS := 
    PLATFORM_SOURCES := webcam_v4l2.c
else
    # Other platforms: Use fallback
    PLATFORM_LDFLAGS := 
    PLATFORM_SOURCES := 
endif

override LDFLAGS := $(PKG_LDFLAGS) -lpthread $(PLATFORM_LDFLAGS)

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

# Source code files
C_FILES   := $(wildcard *.c)

# Header files
HEADERS     := $(wildcard *.h)

# Object files for server and client  
C_FILES := $(wildcard *.c)
M_FILES := $(wildcard *.m)

# Object files (binaries)
OBJS_C    := $(patsubst %.c,   $(BUILD_DIR)/%.o, $(C_FILES))
OBJS_M    := $(patsubst %.m,   $(BUILD_DIR)/%.o, $(M_FILES))

# All object files for server and client
OBJS := $(OBJS_C) $(OBJS_M)

# Non-target object files (files without main methods)
OBJS_NON_TARGET := $(filter-out $(patsubst $(BIN_DIR)/%, $(BUILD_DIR)/%.o, $(TARGETS)), $(OBJS))

# =============================================================================
# Phony Targets
# =============================================================================

.PHONY: all clean default help debug release c-objs format format-check bear clang-tidy

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
debug: $(TARGETS)

# Release build
release: CFLAGS += -O3
release: $(TARGETS)

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/server.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
ifeq ($(shell uname),Darwin)
	$(CC) -o $@ $^ $(LDFLAGS) -sectcreate __TEXT __info_plist Info.plist
else
	$(CC) -o $@ $^ $(LDFLAGS)
endif
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/client.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

# Compile C source files
$(OBJS_C): $(BUILD_DIR)/%.o: %.c $(HEADERS)
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile Objective-C source files
$(OBJS_M): $(BUILD_DIR)/%.o: %.m $(HEADERS)
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $<


c-objs: $(OBJS_C)
	@echo "C object files:"
	@echo $(OBJS_C)
	@echo "C object files count: $(words $(OBJS_C))"
	@echo "C object files size: $(shell du -sh $(OBJS_C) | cut -f1)"
	@echo "C object files count: $(words $(OBJS_C))"

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
	@find . -name "*.c" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format -i; \
	echo "Code formatting complete!"

# Check code formatting
format-check:
	@echo "Checking code formatting..."
	find . -name "*.c" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format --dry-run --Werror

# Run bear to generate a compile_commands.json file
compile_commands.json: Makefile
	@echo "Running bear to generate compile_commands.json..."
	@bear -- make clean debug
	@echo "Bear complete!"

# Run clang-tidy to check code style
clang-tidy: $(wildcard *.c) $(wildcard *.h) $(wildcard *.m)
	@#clang-tidy -header-filter='.*' $^ -- $(BASE_FLAGS) $(FEATURE_FLAGS) $(PKG_CFLAGS)
	@clang-tidy $(wildcard *.c) $(wildcard *.h) $(wildcard *.m) -- $(CFLAGS)


# =============================================================================
# Extra Makefile stuff
# =============================================================================

.PRECIOUS: $(OBJS_NON_TARGET)
