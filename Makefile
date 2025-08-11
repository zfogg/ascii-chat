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

override LDFLAGS := $(PKG_LDFLAGS) -lm -lpthread $(PLATFORM_LDFLAGS)

# Only embed Info.plist on macOS
ifeq ($(shell uname),Darwin)
    INFO_PLIST_FLAGS := -sectcreate __TEXT __info_plist Info.plist
else
    INFO_PLIST_FLAGS :=
endif

# =============================================================================
# SIMD Flags
# =============================================================================
# Detect CPU/OS for reasonable defaults (build-host only)
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# macOS specifics: detect Apple Silicon and Rosetta
ifeq ($(UNAME_S),Darwin)
  IS_APPLE_SILICON := $(shell sysctl -n hw.optional.arm64 2>/dev/null || echo 0)
  IS_ROSETTA       := $(shell sysctl -n sysctl.proc_translated 2>/dev/null || echo 0)
endif

# User override controls
# SIMD_MODE can be one of: auto, off, sse2, avx2, neon, native
SIMD_MODE ?= auto

# Decide an effective mode: either user-provided or autodetected
# Autodetect chooses a conservative baseline suitable for distribution.
ifeq ($(SIMD_MODE),auto)
  # Compute SIMD_MODE_AUTO based on OS/arch; Rosetta counts as x86_64.
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_ROSETTA),1)
      SIMD_MODE_AUTO := sse2
    else ifeq ($(IS_APPLE_SILICON),1)
      SIMD_MODE_AUTO := neon
    else
      SIMD_MODE_AUTO := sse2
    endif
  else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
    SIMD_MODE_AUTO := neon
  else ifeq ($(UNAME_M),x86_64)
    SIMD_MODE_AUTO := sse2
  else ifeq ($(UNAME_M),aarch64)
    SIMD_MODE_AUTO := native
  else
    SIMD_MODE_AUTO := off
  endif
else
  SIMD_MODE_AUTO := $(SIMD_MODE)
endif

# Map the effective mode to compiler flags (single table)
ifneq (,$(filter $(SIMD_MODE_AUTO),off))
  $(info Building without SIMD (scalar))
else ifneq (,$(filter $(SIMD_MODE_AUTO),sse2))
  $(info Using SSE2 baseline)
  SIMD_CFLAGS := -DSIMD_SUPPORT -DSIMD_SUPPORT_SSE2 -msse2
else ifneq (,$(filter $(SIMD_MODE_AUTO),avx2))
  $(info Using AVX2 (ensure target CPUs support it)
  SIMD_CFLAGS := -DSIMD_SUPPORT -DSIMD_SUPPORT_AVX2 -mavx2
else ifneq (,$(filter $(SIMD_MODE_AUTO),neon))
  $(info Using ARM NEON)
  SIMD_CFLAGS := -DSIMD_SUPPORT -DSIMD_SUPPORT_NEON
else ifneq (,$(filter $(SIMD_MODE_AUTO),native))
  $(info Using -march=native (build-host only))
  SIMD_CFLAGS := -DSIMD_SUPPORT -march=native
else
  $(info Unknown architecture $(UNAME_M); building without SIMD)
  SIMD_CFLAGS := 
endif

# Apply to both C and ObjC compilation
override CFLAGS    += $(SIMD_CFLAGS)
override OBJCFLAGS += $(SIMD_CFLAGS)

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

.PHONY: all clean default help debug sanitize release c-objs format format-check bear clang-tidy analyze cloc

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
	$(CC) -o $@ $^ $(LDFLAGS) $(INFO_PLIST_FLAGS)
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
	@make clean && bear -- make debug
	@echo "Bear complete!"

# Run clang-tidy to check code style
clang-tidy: $(C_FILES) $(C_HEADERS) $(M_FILES)
	@#clang-tidy -header-filter='.*' $^ -- $(BASE_FLAGS) $(FEATURE_FLAGS) $(PKG_CFLAGS)
	@clang-tidy $^ -- $(CFLAGS)

analyze:
	clang --analyze $(SOURCES)
	cppcheck --enable=all $(C_FILES) $(C_HEADERS)

cloc:
	cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' .

# =============================================================================
# Extra Makefile stuff
# =============================================================================

.PRECIOUS: $(OBJS_NON_TARGET)
