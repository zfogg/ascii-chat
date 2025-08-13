#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
CC  := clang

# Package dependencies
PKG_CONFIG_LIBS := zlib portaudio-2.0

# Directories
BIN_DIR   := bin
BUILD_DIR := build
SRC_DIR   := src
LIB_DIR   := lib

override CFLAGS += -I$(LIB_DIR) -I$(SRC_DIR)

# =============================================================================
# Compiler Flags
# =============================================================================

CSTD := c23

# Base flags
override CFLAGS += -Wall -Wextra

# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
override CFLAGS += -D_GNU_SOURCE

# Get package-specific flags
override CFLAGS  += $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
override LDFLAGS += $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

# Platform-specific LDFLAGS for webcam library
ifeq ($(shell uname),Darwin)
    # macOS: Add AVFoundation and CoreMedia frameworks
    override LDFLAGS += -framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo
else ifeq ($(shell uname),Linux)
    # Linux: No additional frameworks needed for V4L2
endif

override LDFLAGS += -lm -lpthread

# Avoid leading space from '+=' when LDFLAGS is initially empty
override LDFLAGS += $(ARCH_FLAGS)

# Deduplicate common libs to avoid linker warnings (e.g., duplicate -lm)
override LDFLAGS := $(strip $(filter-out -lm,$(LDFLAGS)) -lm)

# NOTE: set CFLAGS+=-std= ~after~ setting OBJCFLAGS
override OBJCFLAGS += $(CFLAGS)
override CFLAGS += -std=$(CSTD)

# Allow external tools (e.g., scan-build) to inject additional flags without
# fighting our override-based layout. Use EXTRA_CFLAGS for safe augmentation.
EXTRA_CFLAGS ?=
override CFLAGS += $(EXTRA_CFLAGS)

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

# Force arm64 when building natively on Apple Silicon (not under Rosetta)
ifeq ($(UNAME_S),Darwin)
  ifeq ($(IS_APPLE_SILICON),1)
    ifneq ($(IS_ROSETTA),1)
      ARCH_FLAGS := -arch arm64
      $(info Forcing arm64 build on Apple Silicon)
    endif
  endif
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
      SIMD_MODE_AUTO := ssse3
    else ifeq ($(IS_APPLE_SILICON),1)
      SIMD_MODE_AUTO := neon
    else
      SIMD_MODE_AUTO := ssse3
    endif
  else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
    SIMD_MODE_AUTO := neon
  else ifeq ($(UNAME_M),x86_64)
    # Check for x86_64 SIMD support in priority order: AVX2 > SSSE3 > SSE2
    HAS_AVX2 := $(shell grep -q avx2 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
    HAS_SSSE3 := $(shell grep -q ssse3 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
    HAS_SSE2 := $(shell grep -q sse2 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
    ifeq ($(HAS_AVX2),1)
      SIMD_MODE_AUTO := avx2
    else ifeq ($(HAS_SSSE3),1)
      SIMD_MODE_AUTO := ssse3
    else ifeq ($(HAS_SSE2),1)
      SIMD_MODE_AUTO := sse2
    else
      SIMD_MODE_AUTO := off
    endif
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
else ifneq (,$(filter $(SIMD_MODE_AUTO),ssse3))
  $(info Using SSSE3 with 32-pixel processing)
  SIMD_CFLAGS := -DSIMD_SUPPORT -DSIMD_SUPPORT_SSE2 -DSIMD_SUPPORT_SSSE3 -msse2 -mssse3
else ifneq (,$(filter $(SIMD_MODE_AUTO),avx2))
  $(info Using AVX2 + SSSE3 + SSE2 (best x86_64 performance))
  SIMD_CFLAGS := -DSIMD_SUPPORT -DSIMD_SUPPORT_SSE2 -DSIMD_SUPPORT_SSSE3 -DSIMD_SUPPORT_AVX2 -msse2 -mssse3 -mavx2
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

override CFLAGS    += $(ARCH_FLAGS) $(SIMD_CFLAGS)
override OBJCFLAGS += $(ARCH_FLAGS) $(SIMD_CFLAGS)

# =============================================================================
# CPU-aware Optimization Flags
# =============================================================================

# CPU-aware optimization flags for all platforms
ifeq ($(UNAME_S),Darwin)
    # macOS: avoid -mcpu when targeting x86_64 (Rosetta); use generic flags on Apple Silicon
    ifeq ($(IS_ROSETTA),1)
        CPU_OPT_FLAGS := -O3 -march=native
        $(info Using Rosetta (x86_64) optimizations: $(CPU_OPT_FLAGS))
    else ifeq ($(IS_APPLE_SILICON),1)
        CPU_OPT_FLAGS := -O3 -ffast-math
        $(info Using Apple Silicon optimizations: $(CPU_OPT_FLAGS))
    else
        CPU_OPT_FLAGS := -O3 -march=native
        $(info Using Intel Mac optimizations: $(CPU_OPT_FLAGS))
    endif
else ifeq ($(UNAME_S),Linux)
    # Linux: CPU-specific optimizations
    ifeq ($(UNAME_M),aarch64)
        CPU_OPT_FLAGS := -O3 -mcpu=native
        $(info Using Linux ARM64 optimizations: $(CPU_OPT_FLAGS))
    else
        CPU_OPT_FLAGS := -O3 -march=native
        $(info Using Linux x86_64 optimizations: $(CPU_OPT_FLAGS))
    endif
else
    # Other platforms: Generic -O3
    CPU_OPT_FLAGS := -O3
    $(info Using generic optimizations: $(CPU_OPT_FLAGS))
endif

# Compose per-config flags cleanly (no filter-out hacks)
DEBUG_FLAGS    := -g -O0 -DDEBUG -DDEBUG_MEMORY
RELEASE_FLAGS  := $(CPU_OPT_FLAGS) -DNDEBUG -funroll-loops
SANITIZE_FLAGS := -fsanitize=address

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

# Source code files
C_FILES := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)

# Header files
C_HEADERS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(LIB_DIR)/*.h)

SOURCES := $(C_FILES) $(M_FILES) $(C_HEADERS)

# Object files (binaries)
OBJS_C := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
          $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
          $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# All object files for server and client
OBJS := $(OBJS_C) $(OBJS_M)

# Non-target object files (files without main methods)
OBJS_NON_TARGET := $(filter-out $(BUILD_DIR)/src/server.o $(BUILD_DIR)/src/client.o, $(OBJS))

# =============================================================================
# Build Rules
# =============================================================================

# Main targets
default: $(TARGETS)
all: default

# Debug build
debug: override CFLAGS    += $(DEBUG_FLAGS)
debug: override OBJCFLAGS += $(DEBUG_FLAGS)
debug: $(TARGETS)

# Release build
release: override CFLAGS    += $(RELEASE_FLAGS)
release: override OBJCFLAGS += $(RELEASE_FLAGS)
release: $(TARGETS)

# Memory sanitizer build (inherits debug flags)
sanitize: override CFLAGS    += $(DEBUG_FLAGS) $(SANITIZE_FLAGS)
sanitize: override OBJCFLAGS += $(DEBUG_FLAGS) $(SANITIZE_FLAGS)
sanitize: override LDFLAGS   +=                $(SANITIZE_FLAGS)
sanitize: $(TARGETS)

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/src/server.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/src/client.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS) $(INFO_PLIST_FLAGS)
	@echo "Built $@ successfully!"

# Compile C source files from src/
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/src
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile C source files from lib/
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/lib
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $< 

# Compile Objective-C source files from lib/
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/lib
	@echo "Compiling $<..."
	$(CC) -o $@ $(OBJCFLAGS) -c $<

# For CI
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
	@echo "  all/default     - Build all targets with default flags"
	@echo "  debug           - Build with debug symbols and no optimization"
	@echo "  release         - Build with optimizations enabled"
	@echo "  format          - Format source code using clang-format"
	@echo "  format-check    - Check code formatting without modifying files"
	@echo "  clang-tidy      - Run clang-tidy on sources"
	@echo "  analyze         - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc            - Count lines of code"
	@echo "  todo            - Build the ./todo subproject"
	@echo "  todo-clean      - Clean the ./todo subproject"
	@echo "  clean           - Remove build artifacts"
	@echo "  help            - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  CC=$(CC)"
	@echo "  PKG_CONFIG_LIBS=$(PKG_CONFIG_LIBS)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  LDFLAGS=$(LDFLAGS)"

# =============================================================================
# Code Formatting
# =============================================================================

# Format source code
format:
	@echo "Formatting source code..."
	@find $(SRC_DIR) $(LIB_DIR) -name "*.c" -o -name "*.h" | \
    xargs clang-format --Werror -i; \
	echo "Code formatting complete!"

# Check code formatting
format-check:
	@echo "Checking code formatting..."
	find $(SRC_DIR) $(LIB_DIR) -name "*.c" -o -name "*.h" | \
    xargs clang-format --dry-run --Werror

# Run bear to generate a compile_commands.json file
compile_commands.json: Makefile
	@echo "Running bear to generate compile_commands.json..."
	@make clean && bear -- make debug todo
	@echo "Bear complete!"

# Run clang-tidy to check code style
clang-tidy: compile_commands.json
	@echo "Running clang-tidy with compile_commands.json..."
	clang-tidy -p . -header-filter='.*' $(C_FILES) $(M_FILES) -- $(CFLAGS)

analyze:
	@echo "Running clang static analysis (C sources)..."
	clang --analyze $(CFLAGS) $(C_FILES)
	@echo "Running clang static analysis (Objective-C sources)..."
	clang --analyze $(OBJCFLAGS) $(M_FILES)
	@echo "Running cppcheck..."
	cppcheck --enable=all --inline-suppr \
		-I$(LIB_DIR) -I$(SRC_DIR) $(shell pkg-config --cflags-only-I $(PKG_CONFIG_LIBS)) \
		--suppress=missingIncludeSystem \
		$(C_FILES) $(C_HEADERS)

scan-build: c-objs
	@echo "Running scan-build with EXTRA_CFLAGS='-Wformat -Wformat-security -Werror=format-security'..."
	scan-build --status-bugs -analyze-headers make clean
	scan-build --status-bugs -analyze-headers make CSTD="$(CSTD)" EXTRA_CFLAGS="-Wformat -Wformat-security -Werror=format-security" c-objs

cloc:
	cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' .

# =============================================================================
# Subprojects
# =============================================================================

todo:
	@if [ -f todo/Makefile_simd ]; then \
		$(MAKE) -C todo -f Makefile_simd all; \
	fi
	@if [ -f todo/Makefile_rate_limiter ]; then \
		$(MAKE) -C todo -f Makefile_rate_limiter all; \
	fi

todo-clean:
	@if [ -f todo/Makefile_simd ]; then \
		$(MAKE) -C todo -f Makefile_simd clean || true; \
	fi
	@if [ -f todo/Makefile_rate_limiter ]; then \
		$(MAKE) -C todo -f Makefile_rate_limiter clean || true; \
	fi

# =============================================================================
# Extra Makefile stuff
# =============================================================================

.DEFAULT_GOAL := debug

.PRECIOUS: $(OBJS_NON_TARGET)

.PHONY: all clean default help debug sanitize release c-objs format format-check bear clang-tidy analyze scan-build cloc todo todo-clean
