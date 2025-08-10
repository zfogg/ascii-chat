#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
override CC  := clang

# Package dependencies
PKG_CONFIG_LIBS := zlib portaudio-2.0

# Directories
BIN_DIR   := bin
BUILD_DIR := build
SRC_DIR   := src
LIB_DIR   := lib

# =============================================================================
# Compiler Flags
# =============================================================================

CSTD := c23

# Base flags
BASE_FLAGS := -Wall -Wextra

# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
C_FEATURE_FLAGS := -D_GNU_SOURCE

override CFLAGS    += $(BASE_FLAGS) $(C_FEATURE_FLAGS) -I$(LIB_DIR)
override OBJCFLAGS += $(BASE_FLAGS)

# Get package-specific flags
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))

PKG_LDFLAGS := $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

override CFLAGS   += $(PKG_CFLAGS) -std=$(CSTD)
override OBJCFLAGS +=

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

override LDFLAGS += $(PKG_LDFLAGS) -lpthread $(PLATFORM_LDFLAGS)

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

# Source code files
C_FILES := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)

# Header files
C_HEADERS     := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(LIB_DIR)/*.h)

SOURCES := $(C_FILES) $(M_FILES) $(C_HEADERS)

# Object files (binaries)
OBJS_C    := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
             $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M    := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
             $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# All object files for server and client
OBJS := $(OBJS_C) $(OBJS_M)

# Non-target object files (files without main methods)
OBJS_NON_TARGET := $(filter-out $(BUILD_DIR)/src/server.o $(BUILD_DIR)/src/client.o, $(OBJS))

# =============================================================================
# Phony Targets
# =============================================================================

.-HONY: all clean default help debug sanitize release c-objs format format-check bear clang-tidy analyze

# =============================================================================
# Default Target
# =============================================================================

.DEFAULT_GOAL := debug

# =============================================================================
# Build Rules
# =============================================================================

# Main targets
default: $(TARGETS)
all: default

# Debug build
debug: CFLAGS += -g -O0 -DDEBUG -DDEBUG_MEMORY
debug: OBJCFLAGS += -g -O0 -DDEBUG -DDEBUG_MEMORY
debug: $(TARGETS)

# Memory sanitizer build
sanitize: debug 
sanitize: CFLAGS += -fsanitize=address
sanitize: LDFLAGS += -fsanitize=address 
sanitize: $(TARGETS)

# Release build
release: CFLAGS += -O3
release: $(TARGETS)

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/src/server.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/src/client.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS) -sectcreate __TEXT __info_plist Info.plist
	@echo "Built $@ successfully!"

# Create build directories
$(BUILD_DIR)/src $(BUILD_DIR)/lib:
	@mkdir -p $@

# Compile C source files from src/
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/src
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile C source files from lib/
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/lib
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile Objective-C source files from src/
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/src
	@echo "Compiling $<..."
	$(CC) -o $@ $(OBJCFLAGS) -c $<

# Compile Objective-C source files from lib/
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/lib
	@echo "Compiling $<..."
	$(CC) -o $@ $(OBJCFLAGS) -c $<


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
	@find $(SRC_DIR) $(LIB_DIR) -name "*.c" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format -i; \
	echo "Code formatting complete!"

# Check code formatting
format-check:
	@echo "Checking code formatting..."
	find $(SRC_DIR) $(LIB_DIR) -name "*.c" -o -name "*.h" -o -name "*.hpp" | \
	xargs clang-format --dry-run --Werror

# Run bear to generate a compile_commands.json file
compile_commands.json: Makefile
	@echo "Running bear to generate compile_commands.json..."
	@bear -- make clean debug
	@echo "Bear complete!"

# Run clang-tidy to check code style
clang-tidy: $(C_FILES) $(C_HEADERS) $(M_FILES)
	@#clang-tidy -header-filter='.*' $^ -- $(BASE_FLAGS) $(FEATURE_FLAGS) $(PKG_CFLAGS)
	@clang-tidy $^ -- $(CFLAGS)

analyze:
	clang --analyze $(SOURCES)
	cppcheck --enable=all $(C_FILES) $(C_HEADERS)

# =============================================================================
# Extra Makefile stuff
# =============================================================================

.PRECIOUS: $(OBJS_NON_TARGET)
