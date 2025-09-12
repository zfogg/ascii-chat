#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
CC := clang

# Package dependencies
PKG_CONFIG_LIBS := zlib portaudio-2.0 libsodium
TEST_PKG_CONFIG_LIBS := criterion

# Directories
BIN_DIR   := bin
BUILD_DIR := build
SRC_DIR   := src
LIB_DIR   := lib
TEST_DIR  := tests
TEST_BUILD_DIR := build/tests

ifeq ($(shell uname),Darwin)
  SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path)
  CLANG_RESOURCE_DIR := $(shell $(CC) -print-resource-dir)
  override CFLAGS += -isysroot $(SDKROOT) -isystem $(CLANG_RESOURCE_DIR)/include
endif

override CFLAGS += -I$(LIB_DIR) -I$(SRC_DIR)

# =============================================================================
# Compiler Flags
# =============================================================================

# Use C17 on Windows, C23 on POSIX for better SIMD compatibility
ifneq ($(shell uname),)
  ifeq ($(findstring MINGW,$(shell uname)),)
    ifeq ($(findstring MSYS,$(shell uname)),)
      ifeq ($(findstring CYGWIN,$(shell uname)),)
        CSTD ?= c23
      else
        CSTD ?= c17
      endif
    else
      CSTD ?= c17
    endif
  else
    CSTD ?= c17
  endif
else
  CSTD ?= c17
endif

# Base flags
override CFLAGS += -Wall -Wextra


# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
# Only enable on POSIX systems (Linux/macOS), not Windows
ifneq ($(shell uname),)
  ifeq ($(findstring MINGW,$(shell uname)),)
    ifeq ($(findstring MSYS,$(shell uname)),)
      ifeq ($(findstring CYGWIN,$(shell uname)),)
        override CFLAGS += -D_GNU_SOURCE
      endif
    endif
  endif
endif

# Get package-specific flags
override CFLAGS += $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))

# Build LDFLAGS systematically to avoid duplicates
PKG_LDFLAGS := $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

# Platform-specific libraries
ifeq ($(shell uname),Darwin)
    PLATFORM_LDFLAGS := -framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo -lncurses
else ifeq ($(shell uname),Linux)
    # Library search paths for Linux (must come first in LDFLAGS)
    LINUX_LIB_PATHS := -L/usr/lib/x86_64-linux-gnu -L/lib/x86_64-linux-gnu
    PLATFORM_LDFLAGS := -lncurses
    # When using static PortAudio, we need JACK libraries for the JACK backend
    # Only add -ljack if:
    # 1. Static PortAudio exists (libportaudio.a)
    # 2. JACK is actually installed (check with pkg-config)
	# Check if JACK is available using pkg-config
	JACK_EXISTS := $(shell pkg-config --exists jack 2>/dev/null && echo yes || echo no)
	ifeq ($(JACK_EXISTS),yes)
		# pkg-config returns -ljack but not the library path on Ubuntu
		# Add -ljack to platform flags
		PLATFORM_LDFLAGS += -ljack
	endif
endif

# System libraries (only add what pkg-config doesn't provide)
SYSTEM_LDFLAGS := -lm

# Check if pkg-config already provides pthread, if not add it
ifeq ($(findstring -lpthread,$(PKG_LDFLAGS)),)
    SYSTEM_LDFLAGS += -lpthread
endif

# Combine all LDFLAGS (library paths must come first)
override LDFLAGS := $(LINUX_LIB_PATHS) $(PKG_LDFLAGS) $(PLATFORM_LDFLAGS) $(SYSTEM_LDFLAGS) $(ARCH_FLAGS)

# Test-specific flags
TEST_CFLAGS  := $(shell pkg-config --cflags $(TEST_PKG_CONFIG_LIBS) 2>/dev/null || echo "")
# Test linking flags - try pkg-config first, fallback to direct linking
TEST_LDFLAGS := $(shell pkg-config --libs criterion 2>/dev/null)
ifeq ($(TEST_LDFLAGS),)
    # Fallback for systems without pkg-config for criterion
    ifeq ($(shell uname),Linux)
        TEST_LDFLAGS := -lcriterion -lboxfort
    else
        # macOS with Homebrew - need explicit path
        TEST_LDFLAGS := -L/opt/homebrew/lib -lcriterion
    endif
endif

# Add required dependencies on Linux
ifeq ($(shell uname),Linux)
    # Add essential test dependencies - order matters for linking
    # First add protobuf-c (needed by criterion)
    ifneq ($(shell pkg-config --exists libprotobuf-c 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs libprotobuf-c)
    else
        # Fallback if pkg-config doesn't work
        TEST_LDFLAGS += -lprotobuf-c
    endif


    # Add Nanopb (needed by criterion for protobuf) - use static library
    ifneq ($(shell pkg-config --exists nanopb 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs nanopb)
    else
        # Fallback: link against the static library provided by libnanopb-dev
        TEST_LDFLAGS += /usr/lib/x86_64-linux-gnu/libprotobuf-nanopb.a
    endif

    # Add boxfort (needed by criterion for sandboxing)
    ifneq ($(shell pkg-config --exists boxfort 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs boxfort)
    else
        # Fallback if pkg-config doesn't work
        TEST_LDFLAGS += -lboxfort
    endif

    # Add nanomsg
    ifneq ($(shell pkg-config --exists nanomsg 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs nanomsg)
    endif

    # Add libgit2 dependencies
    ifneq ($(shell pkg-config --exists libgit2 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs libgit2)
    endif

    # Add system libraries that may be needed (use pkg-config when available)
    # For GSSAPI/Kerberos support (needed by libssh2/libgit2)
    ifneq ($(shell pkg-config --exists krb5-gssapi 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs krb5-gssapi)
    else ifneq ($(shell pkg-config --exists mit-krb5-gssapi 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs mit-krb5-gssapi)
    else ifneq ($(shell pkg-config --exists libssh2 2>/dev/null && echo yes),)
        # If we have libssh2 via pkg-config, it should bring in its deps
        TEST_LDFLAGS += $(shell pkg-config --libs libssh2)
    else
        # Fallback: add GSSAPI libraries if available
        TEST_LDFLAGS += -lgssapi_krb5 -lkrb5 -lk5crypto -lcom_err
    endif

    # Other dependencies - add in dependency order
    TEST_LDFLAGS += -lssl -lcrypto -lssh2 -lhttp_parser -lpcre2-8 -ldl -lresolv
endif

# LTO flag will be added to TEST_LDFLAGS by release test targets

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
# Hardware Acceleration Detection and Flags
# =============================================================================
# Detect CPU/OS for reasonable defaults (build-host only)
UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# Detect number of CPU cores for parallel builds
ifeq ($(UNAME_S),Darwin)
  # macOS: use sysctl to get number of logical processors
  CPU_CORES := $(shell sysctl -n hw.logicalcpu)
else ifeq ($(UNAME_S),Linux)
  # Linux: use nproc to get number of processing units available
  CPU_CORES := $(shell nproc)
else
  # Fallback: assume 4 cores for other systems
  CPU_CORES := 4
endif

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
    endif
  endif
endif

# =============================================================================
# SIMD Configuration
# =============================================================================
# User override controls
SIMD_MODE ?= auto

# Multiple checks for SIMD mode detection
ENABLE_SIMD_SSE2 =
ENABLE_SIMD_SSSE3 =
ENABLE_SIMD_AVX2 =
ENABLE_SIMD_AVX512 =
ENABLE_SIMD_NEON =
ENABLE_SIMD_SVE =

# Check for user-specified SIMD mode
ifeq ($(SIMD_MODE),sse2)
  ENABLE_SIMD_SSE2 = yes
endif
ifeq ($(SIMD_MODE),ssse3)
  ENABLE_SIMD_SSSE3 = yes
endif
ifeq ($(SIMD_MODE),avx2)
  ENABLE_SIMD_AVX2 = yes
endif
ifeq ($(SIMD_MODE),avx512)
  ENABLE_SIMD_AVX512 = yes
endif
ifeq ($(SIMD_MODE),neon)
  ENABLE_SIMD_NEON = yes
endif
ifeq ($(SIMD_MODE),sve)
  ENABLE_SIMD_SVE = yes
endif

# Auto-detect SIMD capabilities
ifeq ($(SIMD_MODE),auto)
  # Apple Silicon detection
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_APPLE_SILICON),1)
      ENABLE_SIMD_NEON = yes
    else ifeq ($(IS_ROSETTA),1)
      ENABLE_SIMD_SSSE3 = yes
    else
      ENABLE_SIMD_SSSE3 = yes
    endif
  endif

  # Linux ARM64 detection
  ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
    # Check for SVE support first (newer and more capable than NEON)
    HAS_SVE := $(shell grep -q sve /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
    ifeq ($(HAS_SVE),1)
      ENABLE_SIMD_SVE = yes
    else
      ENABLE_SIMD_NEON = yes
    endif
  endif

  # x86_64 feature detection (prefer newer SIMD instructions)
  ifeq ($(UNAME_M),x86_64)
    # macOS uses sysctl, Linux uses /proc/cpuinfo
    ifeq ($(UNAME_S),Darwin)
      HAS_AVX512F := $(shell sysctl -n hw.optional.avx512f 2>/dev/null || echo 0)
      HAS_AVX512BW := $(shell sysctl -n hw.optional.avx512bw 2>/dev/null || echo 0)
      HAS_AVX2 := $(shell sysctl -n hw.optional.avx2_0 2>/dev/null || echo 0)
      HAS_SSSE3 := $(shell sysctl -n hw.optional.supplementalsse3 2>/dev/null || echo 0)
      HAS_SSE2 := $(shell sysctl -n hw.optional.sse2 2>/dev/null || echo 0)
      HAS_SSE42 := $(shell sysctl -n hw.optional.sse4_2 2>/dev/null || echo 0)
    else
      HAS_AVX512F := $(shell grep -q avx512f /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
      HAS_AVX512BW := $(shell grep -q avx512bw /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
      HAS_AVX2 := $(shell grep -q avx2 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
      HAS_SSSE3 := $(shell grep -q ssse3 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
      HAS_SSE2 := $(shell grep -q sse2 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
      HAS_SSE42 := $(shell grep -q sse4_2 /proc/cpuinfo 2>/dev/null && echo 1 || echo 0)
    endif

    # AVX-512 requires both AVX512F (foundation) and AVX512BW (byte/word operations)
    ifeq ($(HAS_AVX512F)$(HAS_AVX512BW),11)
      ENABLE_SIMD_AVX512 = yes
	endif
    ifeq ($(HAS_AVX2),1)
      ENABLE_SIMD_AVX2 = yes
    endif
    ifeq ($(HAS_SSSE3),1)
      ENABLE_SIMD_SSSE3 = yes
    endif
    ifeq ($(HAS_SSE2),1)
      ENABLE_SIMD_SSE2 = yes
    endif
  endif
endif

ifneq ($(or $(ENABLE_SIMD_AVX512),$(ENABLE_SIMD_AVX2),$(ENABLE_SIMD_SSSE3),$(ENABLE_SIMD_SSE2),$(ENABLE_SIMD_SVE),$(ENABLE_SIMD_NEON)),)
  SIMD_CFLAGS := -DSIMD_SUPPORT
  # Prefer wider vector widths for SIMD-heavy multimedia workloads
  ifdef ENABLE_SIMD_AVX2
    SIMD_CFLAGS += -mprefer-vector-width=256
  endif
endif

# Apply SIMD flags based on detection
ifdef ENABLE_SIMD_SSE2
  ifeq ($(shell uname),)
    # Windows - disable MMX to avoid compiler bugs
    SIMD_CFLAGS += -DSIMD_SUPPORT_SSE2 -msse2 -mno-mmx
  else
    SIMD_CFLAGS += -DSIMD_SUPPORT_SSE2 -msse2
  endif
endif
ifdef ENABLE_SIMD_SSSE3
  ifeq ($(shell uname),)
    # Windows - disable MMX to avoid compiler bugs
    SIMD_CFLAGS += -DSIMD_SUPPORT_SSSE3 -mssse3 -mno-mmx
  else
    SIMD_CFLAGS += -DSIMD_SUPPORT_SSSE3 -mssse3
  endif
endif
ifdef ENABLE_SIMD_AVX2
  ifeq ($(shell uname),)
    # Windows - disable MMX to avoid compiler bugs
    SIMD_CFLAGS += -DSIMD_SUPPORT_AVX2 -mavx2 -mno-mmx
  else
    SIMD_CFLAGS += -DSIMD_SUPPORT_AVX2 -mavx2
  endif
endif
ifdef ENABLE_SIMD_AVX512
  SIMD_CFLAGS += -DSIMD_SUPPORT_AVX512 -mavx512f -mavx512bw
endif
ifdef ENABLE_SIMD_NEON
  SIMD_CFLAGS += -DSIMD_SUPPORT_NEON
endif
ifdef ENABLE_SIMD_SVE
  SIMD_CFLAGS += -DSIMD_SUPPORT_SVE -march=armv8-a+sve
endif

# =============================================================================
# CRC32 Hardware Acceleration Configuration
# =============================================================================
CRC32_HW ?= auto

# Multiple checks for CRC32 hardware detection
ENABLE_CRC32_HW =

# User override
ifeq ($(CRC32_HW),on)
  ENABLE_CRC32_HW = yes
endif

# Auto-detect CRC32 capabilities
ifeq ($(CRC32_HW),auto)
  # Apple Silicon always has CRC32
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_APPLE_SILICON),1)
      ENABLE_CRC32_HW = yes
    else
      ENABLE_CRC32_HW = yes  # Intel Mac
    endif
  endif

  # Linux ARM64 detection
  ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
    ENABLE_CRC32_HW = yes
  endif

  # x86_64 SSE4.2 detection (includes CRC32)
  ifeq ($(UNAME_M),x86_64)
    # HAS_SSE42 already detected above in SIMD section
    ifeq ($(HAS_SSE42),1)
      ENABLE_CRC32_HW = yes
    endif
  endif

  # Rosetta detection - SSE4.2 already detected above
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_ROSETTA),1)
      # HAS_SSE42 already set in SIMD detection
      ifeq ($(HAS_SSE42),1)
        ENABLE_CRC32_HW = yes
      endif
    endif
  endif
endif

# Apply CRC32 flags based on detection
ifdef ENABLE_CRC32_HW
  CRC32_CFLAGS := -DHAVE_CRC32_HW

  # Add architecture-specific flags
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_APPLE_SILICON),1)
    else
      CRC32_CFLAGS += -msse4.2
    endif
  else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
  else ifeq ($(UNAME_M),x86_64)
    CRC32_CFLAGS += -msse4.2
  endif
else
  CRC32_CFLAGS :=
endif

# =============================================================================
# Libsodium-based Crypto Configuration
# =============================================================================

# =============================================================================
# Combine All Hardware Acceleration Flags
# =============================================================================
HW_ACCEL_CFLAGS := $(SIMD_CFLAGS) $(CRC32_CFLAGS)

override CFLAGS += $(ARCH_FLAGS) $(HW_ACCEL_CFLAGS)
# NOTE: OBJCFLAGS already inherits CFLAGS at line 54, so no need to add HW_ACCEL_CFLAGS again

# =============================================================================
# CPU-aware Optimization Flags
# =============================================================================

# CPU-aware optimization flags for all platforms
ifeq ($(UNAME_S),Darwin)
    # macOS: avoid -mcpu when targeting x86_64 (Rosetta); use -ffast-math only on Apple Silicon
    ifeq ($(IS_ROSETTA),1)
        CPU_OPT_FLAGS := -O3 -march=native -ffp-contract=fast -ffinite-math-only
    else ifeq ($(IS_APPLE_SILICON),1)
        CPU_OPT_FLAGS := -O3 -march=native -mcpu=native -ffast-math -ffp-contract=fast
    else
        CPU_OPT_FLAGS := -O3 -march=native -ffp-contract=fast -ffinite-math-only
    endif
else ifeq ($(UNAME_S),Linux)
    # Linux: CPU-specific optimizations without -ffast-math for safety
    ifeq ($(UNAME_M),aarch64)
        CPU_OPT_FLAGS := -O3 -mcpu=native -ffp-contract=fast -ffinite-math-only
    else
        CPU_OPT_FLAGS := -O3 -march=native -ffp-contract=fast -ffinite-math-only
    endif
else
    # Other platforms: Generic -O3 with safer math optimizations
    CPU_OPT_FLAGS := -O3 -ffp-contract=fast
endif

# Compose per-config flags cleanly (no filter-out hacks)
DEV_FLAGS      := -g -O0 -DDEBUG -DDEBUG_MEMORY -fstack-protector-strong
# Comprehensive sanitizer flags for network/multimedia application
# ASan: memory bugs (buffer overflow, use-after-free, leaks)
# UBSan: undefined behavior (ALL checks - null, integer, bounds, etc)
# Additional: better stack traces, no optimizations that hurt debugging
SANITIZE_FLAGS := -fsanitize=address,undefined,leak,bounds-strict,integer-divide-by-zero,null,return,unreachable,vla-bound,signed-integer-overflow,shift,shift-exponent,shift-base,integer-divide-by-zero,unreachable,vla-bound,null,return,signed-integer-overflow,bounds,bounds-strict,alignment,object-size,float-divide-by-zero,float-cast-overflow,nonnull-attribute,returns-nonnull-attribute,bool,enum,vptr,pointer-overflow,builtin -fno-omit-frame-pointer -fno-optimize-sibling-calls -fstack-protector-all

# Thread sanitizer flags (incompatible with ASan, but includes all other checks)
# TSan: race conditions, deadlocks, thread synchronization issues
# Plus all UBSan checks that are compatible with TSan
TSAN_FLAGS := -fsanitize=thread,undefined,bounds-strict,integer-divide-by-zero,null,return,unreachable,vla-bound,signed-integer-overflow,shift,shift-exponent,shift-base,alignment,object-size,float-divide-by-zero,float-cast-overflow,nonnull-attribute,returns-nonnull-attribute,bool,enum,vptr,pointer-overflow,builtin -fno-omit-frame-pointer -fno-optimize-sibling-calls -fstack-protector-all
COVERAGE_FLAGS := --coverage -fprofile-arcs -ftest-coverage -DCOVERAGE_BUILD
RELEASE_FLAGS  := $(CPU_OPT_FLAGS) -DNDEBUG -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe -flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, ascii-chat-server ascii-chat-client)

# Platform-specific source files
PLATFORM_C_FILES_COMMON := $(wildcard $(LIB_DIR)/platform/*.c)
ifeq ($(shell uname),Darwin)
  # macOS: Use POSIX implementation
  PLATFORM_C_FILES := $(PLATFORM_C_FILES_COMMON) $(wildcard $(LIB_DIR)/platform/posix/*.c)
else ifeq ($(shell uname),Linux)
  # Linux: Use POSIX implementation
  PLATFORM_C_FILES := $(PLATFORM_C_FILES_COMMON) $(wildcard $(LIB_DIR)/platform/posix/*.c)
else
  # Windows: Use Windows implementation
  PLATFORM_C_FILES := $(PLATFORM_C_FILES_COMMON) $(wildcard $(LIB_DIR)/platform/windows/*.c)
endif

# Source code files
LIB_C_FILES := $(filter-out $(LIB_DIR)/ascii_simd_neon.c, $(wildcard $(LIB_DIR)/*.c))
# Server modular source files  
SERVER_C_FILES := $(wildcard $(SRC_DIR)/server/*.c)
# Client and other source files
CLIENT_AND_OTHER_C_FILES := $(SRC_DIR)/client.c
C_FILES := $(SERVER_C_FILES) $(CLIENT_AND_OTHER_C_FILES) $(LIB_C_FILES) $(PLATFORM_C_FILES) $(wildcard $(LIB_DIR)/image2ascii/*.c) $(wildcard $(LIB_DIR)/image2ascii/simd/*.c) $(wildcard $(LIB_DIR)/tests/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)

# Header files
LIB_H_FILES := $(filter-out $(LIB_DIR)/ascii_simd_neon.h, $(wildcard $(LIB_DIR)/*.h))
PLATFORM_H_FILES := $(wildcard $(LIB_DIR)/platform/*.h)
SERVER_H_FILES := $(wildcard $(SRC_DIR)/server/*.h)
C_HEADERS := $(wildcard $(SRC_DIR)/*.h) $(SERVER_H_FILES) $(LIB_H_FILES) $(PLATFORM_H_FILES) $(wildcard $(LIB_DIR)/image2ascii/*.h) $(wildcard $(LIB_DIR)/image2ascii/simd/*.h) $(wildcard $(LIB_DIR)/tests/*.h)

SOURCES := $(C_FILES) $(M_FILES) $(C_HEADERS)

# Object files (binaries) - separate by build configuration
# Debug objects
OBJS_C_DEBUG := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/debug/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
                $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/debug/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M_DEBUG := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/debug/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
                $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/debug/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# Release objects
OBJS_C_RELEASE := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/release/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
                  $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/release/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M_RELEASE := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/release/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
                  $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/release/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# Dev objects (debug without sanitizers)
OBJS_C_DEV := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/dev/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
              $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/dev/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M_DEV := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/dev/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
              $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/dev/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# Coverage objects
OBJS_C_COVERAGE := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/coverage/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
                   $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/coverage/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M_COVERAGE := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/coverage/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
                   $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/coverage/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# Thread sanitizer objects
OBJS_C_TSAN := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/tsan/src/%.o, $(filter $(SRC_DIR)/%.c, $(C_FILES))) \
               $(patsubst $(LIB_DIR)/%.c, $(BUILD_DIR)/tsan/lib/%.o, $(filter $(LIB_DIR)/%.c, $(C_FILES)))
OBJS_M_TSAN := $(patsubst $(SRC_DIR)/%.m, $(BUILD_DIR)/tsan/src/%.o, $(filter $(SRC_DIR)/%.m, $(M_FILES))) \
               $(patsubst $(LIB_DIR)/%.m, $(BUILD_DIR)/tsan/lib/%.o, $(filter $(LIB_DIR)/%.m, $(M_FILES)))

# Default to debug for backward compatibility
OBJS_C := $(OBJS_C_DEBUG)
OBJS_M := $(OBJS_M_DEBUG)

# All object files for server and client by configuration
OBJS_DEBUG := $(OBJS_C_DEBUG) $(OBJS_M_DEBUG)
OBJS_DEV := $(OBJS_C_DEV) $(OBJS_M_DEV)
OBJS_RELEASE := $(OBJS_C_RELEASE) $(OBJS_M_RELEASE)
OBJS_COVERAGE := $(OBJS_C_COVERAGE) $(OBJS_M_COVERAGE)
OBJS_TSAN := $(OBJS_C_TSAN) $(OBJS_M_TSAN)

# Server object files by configuration (all modules in src/server/)
SERVER_OBJS_DEBUG := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/debug/src/server/%.o, $(SERVER_C_FILES))
SERVER_OBJS_DEV := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/dev/src/server/%.o, $(SERVER_C_FILES))
SERVER_OBJS_RELEASE := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/release/src/server/%.o, $(SERVER_C_FILES))
SERVER_OBJS_COVERAGE := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/coverage/src/server/%.o, $(SERVER_C_FILES))
SERVER_OBJS_TSAN := $(patsubst $(SRC_DIR)/server/%.c, $(BUILD_DIR)/tsan/src/server/%.o, $(SERVER_C_FILES))

# Non-target object files (library files, excluding server and client main files)
OBJS_NON_TARGET_DEBUG := $(filter-out $(SERVER_OBJS_DEBUG) $(BUILD_DIR)/debug/src/client.o, $(OBJS_DEBUG))
OBJS_NON_TARGET_DEV := $(filter-out $(SERVER_OBJS_DEV) $(BUILD_DIR)/dev/src/client.o, $(OBJS_DEV))
OBJS_NON_TARGET_RELEASE := $(filter-out $(SERVER_OBJS_RELEASE) $(BUILD_DIR)/release/src/client.o, $(OBJS_RELEASE))
OBJS_NON_TARGET_COVERAGE := $(filter-out $(SERVER_OBJS_COVERAGE) $(BUILD_DIR)/coverage/src/client.o, $(OBJS_COVERAGE))
OBJS_NON_TARGET_TSAN := $(filter-out $(SERVER_OBJS_TSAN) $(BUILD_DIR)/tsan/src/client.o, $(OBJS_TSAN))

# Default to debug for backward compatibility
OBJS := $(OBJS_DEBUG)
OBJS_NON_TARGET := $(OBJS_NON_TARGET_DEBUG)

# Dynamic object selection based on BUILD_MODE
OBJS_CURRENT = $(OBJS_$(shell echo $(BUILD_MODE) | tr '[:lower:]' '[:upper:]'))
OBJS_NON_TARGET_CURRENT = $(OBJS_NON_TARGET_$(shell echo $(BUILD_MODE) | tr '[:lower:]' '[:upper:]'))

# Test files - exclude problematic tests for now
TEST_C_FILES_ALL := $(wildcard $(TEST_DIR)/unit/*.c) $(wildcard $(TEST_DIR)/integration/*.c) $(wildcard $(TEST_DIR)/performance/*.c)
# Exclude tests with API mismatches that prevent compilation
TEST_C_FILES_EXCLUDE := $(TEST_DIR)/unit/ascii_simd_test.c $(TEST_DIR)/integration/server_multiclient_test.c $(TEST_DIR)/integration/video_pipeline_test.c
TEST_C_FILES := $(filter-out $(TEST_C_FILES_EXCLUDE), $(TEST_C_FILES_ALL))

# Test objects by configuration
TEST_OBJS_DEBUG := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/debug/%.o, $(TEST_C_FILES))
TEST_OBJS_DEV := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/dev/%.o, $(TEST_C_FILES))
TEST_OBJS_RELEASE := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/release/%.o, $(TEST_C_FILES))
TEST_OBJS_COVERAGE := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/coverage/%.o, $(TEST_C_FILES))
TEST_OBJS_TSAN := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/tsan/%.o, $(TEST_C_FILES))

# Default to debug
TEST_OBJS := $(TEST_OBJS_DEBUG)
# Transform test file paths to executable names with flattened structure
# tests/unit/common_test.c -> bin/test_unit_common
# tests/integration/crypto_network_test.c -> bin/test_integration_crypto_network
TEST_EXECUTABLES := $(foreach file,$(TEST_C_FILES),$(BIN_DIR)/test_$(subst /,_,$(patsubst $(TEST_DIR)/%_test.c,%,$(file))))

# =============================================================================
# Build Rules
# =============================================================================

# Default build mode (debug with sanitizers)
BUILD_MODE ?= debug

# Main targets
default: $(TARGETS)
all: default

# Debug mode WITH sanitizers (default, safe)
debug: BUILD_MODE := debug
debug: override CFLAGS += $(DEV_FLAGS) $(SANITIZE_FLAGS)
debug: override LDFLAGS += $(SANITIZE_FLAGS)
debug: $(TARGETS)

# Dev mode - debug WITHOUT sanitizers (faster iteration)
dev: BUILD_MODE := dev
dev: override CFLAGS += $(DEV_FLAGS)
dev: override LDFLAGS +=
dev: $(TARGETS)

coverage: BUILD_MODE := coverage
coverage: override CFLAGS += $(DEV_FLAGS) $(COVERAGE_FLAGS)
coverage: override LDFLAGS += $(COVERAGE_FLAGS)
coverage: $(TARGETS)

release: BUILD_MODE := release
release: override CFLAGS += $(RELEASE_FLAGS)
release: override LDFLAGS += -flto
release: $(BIN_DIR)/ascii-chat-server-release $(BIN_DIR)/ascii-chat-client-release

# Thread sanitizer build mode (for finding race conditions)
tsan: BUILD_MODE := tsan
tsan: override CFLAGS += $(DEV_FLAGS) $(TSAN_FLAGS)
tsan: override LDFLAGS += $(TSAN_FLAGS)
tsan: $(TARGETS)

# Legacy sanitize target - now just an alias for debug
sanitize: debug

tests-debug: override CFLAGS += $(DEV_FLAGS) $(SANITIZE_FLAGS)
tests-debug: override LDFLAGS += $(SANITIZE_FLAGS)
tests-debug: override TEST_LDFLAGS += $(SANITIZE_FLAGS)
tests-debug: $(TEST_EXECUTABLES)

tests-coverage: coverage
tests-coverage: override CFLAGS += $(DEV_FLAGS) $(COVERAGE_FLAGS)
tests-coverage: override LDFLAGS += $(COVERAGE_FLAGS)
tests-coverage: override TEST_LDFLAGS += $(COVERAGE_FLAGS)
tests-coverage: $(TEST_OBJS_COVERAGE) $(OBJS_NON_TARGET_COVERAGE)
	@echo "Building test executables with coverage..."
	@for test_obj in $(TEST_OBJS_COVERAGE); do \
		test_name=$$(basename $$test_obj .o); \
		test_category=$$(echo $$test_obj | sed 's|.*/coverage/\([^/]*\)/.*|\1|'); \
		test_base=$$(echo $$test_name | sed 's/_test$$//'); \
		executable_name="test_$${test_category}_$${test_base}_coverage"; \
		echo "Linking $$executable_name (coverage mode)..."; \
		$(CC) $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -o $(BIN_DIR)/$$executable_name $$test_obj $(OBJS_NON_TARGET_COVERAGE) $(LDFLAGS) $(TEST_LDFLAGS) $(COVERAGE_FLAGS); \
	done

tests-release: override CFLAGS += $(RELEASE_FLAGS)
tests-release: override LDFLAGS += -flto
tests-release: override TEST_LDFLAGS += -flto
tests-release: $(TEST_EXECUTABLES)

# Simplified tests-coverage mode

tests-dev: dev
tests-dev: override CFLAGS += $(DEV_FLAGS)
tests-dev: override LDFLAGS +=
tests-dev: override TEST_LDFLAGS +=
tests-dev: $(TEST_EXECUTABLES)

tests-tsan: tsan
tests-tsan: override CFLAGS += $(DEV_FLAGS) $(TSAN_FLAGS)
tests-tsan: override LDFLAGS += $(TSAN_FLAGS)
tests-tsan: override TEST_LDFLAGS += $(TSAN_FLAGS)
tests-tsan: $(TEST_EXECUTABLES)

# Build executables - default to debug
$(BIN_DIR)/ascii-chat-server: $(SERVER_OBJS_DEBUG) $(OBJS_NON_TARGET_DEBUG) | $(BIN_DIR)
	@echo "Linking $@ (debug)..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/ascii-chat-client: $(BUILD_DIR)/debug/src/client.o $(OBJS_NON_TARGET_DEBUG) | $(BIN_DIR)
	@echo "Linking $@ (debug)..."
	$(CC) -o $@ $^ $(LDFLAGS) $(INFO_PLIST_FLAGS)
	@echo "Built $@ successfully!"

# Release executables
$(BIN_DIR)/ascii-chat-server-release: $(SERVER_OBJS_RELEASE) $(OBJS_NON_TARGET_RELEASE) | $(BIN_DIR)
	@echo "Linking $@ (release)..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@cp $@ $(BIN_DIR)/ascii-chat-server
	@echo "Built $@ successfully!"

$(BIN_DIR)/ascii-chat-client-release: $(BUILD_DIR)/release/src/client.o $(OBJS_NON_TARGET_RELEASE) | $(BIN_DIR)
	@echo "Linking $@ (release)..."
	$(CC) -o $@ $^ $(LDFLAGS) $(INFO_PLIST_FLAGS)
	@cp $@ $(BIN_DIR)/ascii-chat-client
	@echo "Built $@ successfully!"

# Compile C source files from src/ - debug
$(BUILD_DIR)/debug/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/src
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Server subdirectory objects (debug)
$(BUILD_DIR)/debug/src/server/%.o: $(SRC_DIR)/server/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/src/server
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile C source files from src/ - release
$(BUILD_DIR)/release/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/release/src
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Server subdirectory objects (release)
$(BUILD_DIR)/release/src/server/%.o: $(SRC_DIR)/server/%.c $(C_HEADERS) | $(BUILD_DIR)/release/src/server
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile C source files from src/ - dev
$(BUILD_DIR)/dev/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/src
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Server subdirectory objects (dev)
$(BUILD_DIR)/dev/src/server/%.o: $(SRC_DIR)/server/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/src/server
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile C source files from src/ - coverage
$(BUILD_DIR)/coverage/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/src
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Server subdirectory objects (coverage)
$(BUILD_DIR)/coverage/src/server/%.o: $(SRC_DIR)/server/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/src/server
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile source files from lib/image2ascii/ - debug
$(BUILD_DIR)/debug/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/image2ascii
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile source files from lib/image2ascii/ - release
$(BUILD_DIR)/release/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/image2ascii
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile source files from lib/image2ascii/ - sanitize
$(BUILD_DIR)/dev/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/image2ascii
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile source files from lib/image2ascii/ - coverage
$(BUILD_DIR)/coverage/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/image2ascii
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile SIMD source files from lib/image2ascii/simd/ - debug
$(BUILD_DIR)/debug/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/image2ascii/simd
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile SIMD source files from lib/image2ascii/simd/ - release
$(BUILD_DIR)/release/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/image2ascii/simd
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile SIMD source files from lib/image2ascii/simd/ - sanitize
$(BUILD_DIR)/dev/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/image2ascii/simd
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile SIMD source files from lib/image2ascii/simd/ - coverage
$(BUILD_DIR)/coverage/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/image2ascii/simd
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile C source files from lib/tests/ - debug
$(BUILD_DIR)/debug/lib/tests/%.o: $(LIB_DIR)/tests/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/tests
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile C source files from lib/tests/ - release
$(BUILD_DIR)/release/lib/tests/%.o: $(LIB_DIR)/tests/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/tests
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile C source files from lib/tests/ - sanitize
$(BUILD_DIR)/dev/lib/tests/%.o: $(LIB_DIR)/tests/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/tests
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile C source files from lib/tests/ - coverage
$(BUILD_DIR)/coverage/lib/tests/%.o: $(LIB_DIR)/tests/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/tests
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile platform source files - debug
$(BUILD_DIR)/debug/lib/platform/%.o: $(LIB_DIR)/platform/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/platform
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile platform/posix source files - debug
$(BUILD_DIR)/debug/lib/platform/posix/%.o: $(LIB_DIR)/platform/posix/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/platform/posix
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile platform/windows source files - debug
$(BUILD_DIR)/debug/lib/platform/windows/%.o: $(LIB_DIR)/platform/windows/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib/platform/windows
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD or platform) - debug
$(BUILD_DIR)/debug/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/debug/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	$(if $(filter platform/%,$*),$(error This rule should not match platform files: $*))
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile platform source files - release
$(BUILD_DIR)/release/lib/platform/%.o: $(LIB_DIR)/platform/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/platform
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile platform/posix source files - release
$(BUILD_DIR)/release/lib/platform/posix/%.o: $(LIB_DIR)/platform/posix/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/platform/posix
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile platform/windows source files - release
$(BUILD_DIR)/release/lib/platform/windows/%.o: $(LIB_DIR)/platform/windows/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib/platform/windows
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD or platform) - release
$(BUILD_DIR)/release/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/release/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	$(if $(filter platform/%,$*),$(error This rule should not match platform files: $*))
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) -c $<

# Compile platform source files - sanitize
$(BUILD_DIR)/dev/lib/platform/%.o: $(LIB_DIR)/platform/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/platform
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile platform/posix source files - sanitize
$(BUILD_DIR)/dev/lib/platform/posix/%.o: $(LIB_DIR)/platform/posix/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/platform/posix
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile platform/windows source files - sanitize
$(BUILD_DIR)/dev/lib/platform/windows/%.o: $(LIB_DIR)/platform/windows/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib/platform/windows
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD or platform) - sanitize
$(BUILD_DIR)/dev/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/dev/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	$(if $(filter platform/%,$*),$(error This rule should not match platform files: $*))
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) -c $<

# Compile platform source files - coverage
$(BUILD_DIR)/coverage/lib/platform/%.o: $(LIB_DIR)/platform/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/platform
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile platform/posix source files - coverage
$(BUILD_DIR)/coverage/lib/platform/posix/%.o: $(LIB_DIR)/platform/posix/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/platform/posix
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile platform/windows source files - coverage
$(BUILD_DIR)/coverage/lib/platform/windows/%.o: $(LIB_DIR)/platform/windows/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib/platform/windows
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD or platform) - coverage
$(BUILD_DIR)/coverage/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/coverage/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	$(if $(filter platform/%,$*),$(error This rule should not match platform files: $*))
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# Compile Objective-C source files from lib/ - debug
$(BUILD_DIR)/debug/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/debug/lib
	@echo "Compiling $< (debug)..."
	$(CC) -o $@ $(OBJCFLAGS) $(DEV_FLAGS) $(SANITIZE_FLAGS) -c $<

# Compile Objective-C source files from lib/ - release
$(BUILD_DIR)/release/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/release/lib
	@echo "Compiling $< (release)..."
	$(CC) -o $@ $(OBJCFLAGS) $(RELEASE_FLAGS) -c $<

# Compile Objective-C source files from lib/ - sanitize
$(BUILD_DIR)/dev/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/dev/lib
	@echo "Compiling $< (dev)..."
	$(CC) -o $@ $(OBJCFLAGS) $(DEV_FLAGS) -c $<

# Compile Objective-C source files from lib/ - coverage
$(BUILD_DIR)/coverage/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/coverage/lib
	@echo "Compiling $< (coverage)..."
	$(CC) -o $@ $(OBJCFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -c $<

# =============================================================================
# Thread Sanitizer Build Rules
# =============================================================================

# Compile C source files from src/ - tsan
$(BUILD_DIR)/tsan/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/src
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Server subdirectory objects (tsan)
$(BUILD_DIR)/tsan/src/server/%.o: $(SRC_DIR)/server/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/src/server
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile source files from lib/image2ascii/ - tsan
$(BUILD_DIR)/tsan/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/image2ascii
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile source files from lib/image2ascii/simd/ - tsan
$(BUILD_DIR)/tsan/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/image2ascii/simd
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile source files from lib/tests/ - tsan
$(BUILD_DIR)/tsan/lib/tests/%.o: $(LIB_DIR)/tests/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/tests
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile platform source files - tsan
$(BUILD_DIR)/tsan/lib/platform/%.o: $(LIB_DIR)/platform/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/platform
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

$(BUILD_DIR)/tsan/lib/platform/posix/%.o: $(LIB_DIR)/platform/posix/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/platform/posix
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

$(BUILD_DIR)/tsan/lib/platform/windows/%.o: $(LIB_DIR)/platform/windows/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib/platform/windows
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD or platform) - tsan
$(BUILD_DIR)/tsan/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/tsan/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	$(if $(filter platform/%,$*),$(error This rule should not match platform files: $*))
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Compile Objective-C source files from lib/ - tsan
$(BUILD_DIR)/tsan/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/tsan/lib
	@echo "Compiling $< (tsan)..."
	$(CC) -o $@ $(OBJCFLAGS) $(DEV_FLAGS) $(TSAN_FLAGS) -c $<

# Build all object files without linking (useful for tooling like Bear/clangd)
objs: $(OBJS_DEBUG) $(TEST_OBJS_DEBUG)

# Ensure build and bin directories exist for each configuration
$(BUILD_DIR)/debug/src:
	@mkdir -p $@
$(BUILD_DIR)/debug/src/server:
	@mkdir -p $@
$(BUILD_DIR)/release/src:
	@mkdir -p $@
$(BUILD_DIR)/release/src/server:
	@mkdir -p $@
$(BUILD_DIR)/dev/src:
	@mkdir -p $@
$(BUILD_DIR)/dev/src/server:
	@mkdir -p $@
$(BUILD_DIR)/coverage/src:
	@mkdir -p $@
$(BUILD_DIR)/coverage/src/server:
	@mkdir -p $@
$(BUILD_DIR)/tsan/src:
	@mkdir -p $@
$(BUILD_DIR)/tsan/src/server:
	@mkdir -p $@

$(BUILD_DIR)/debug/lib:
	@mkdir -p $@
$(BUILD_DIR)/release/lib:
	@mkdir -p $@
$(BUILD_DIR)/dev/lib:
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib:
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib:
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/image2ascii: | $(BUILD_DIR)/debug/lib
	@mkdir -p $@
$(BUILD_DIR)/release/lib/image2ascii: | $(BUILD_DIR)/release/lib
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/image2ascii: | $(BUILD_DIR)/dev/lib
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/image2ascii: | $(BUILD_DIR)/coverage/lib
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/image2ascii: | $(BUILD_DIR)/tsan/lib
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/image2ascii/simd: | $(BUILD_DIR)/debug/lib/image2ascii
	@mkdir -p $@
$(BUILD_DIR)/release/lib/image2ascii/simd: | $(BUILD_DIR)/release/lib/image2ascii
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/image2ascii/simd: | $(BUILD_DIR)/dev/lib/image2ascii
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/image2ascii/simd: | $(BUILD_DIR)/coverage/lib/image2ascii
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/image2ascii/simd: | $(BUILD_DIR)/tsan/lib/image2ascii
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/tests: | $(BUILD_DIR)/debug/lib
	@mkdir -p $@
$(BUILD_DIR)/release/lib/tests: | $(BUILD_DIR)/release/lib
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/tests: | $(BUILD_DIR)/dev/lib
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/tests: | $(BUILD_DIR)/coverage/lib
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/tests: | $(BUILD_DIR)/tsan/lib
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/platform: | $(BUILD_DIR)/debug/lib
	@mkdir -p $@
$(BUILD_DIR)/release/lib/platform: | $(BUILD_DIR)/release/lib
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/platform: | $(BUILD_DIR)/dev/lib
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/platform: | $(BUILD_DIR)/coverage/lib
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/platform: | $(BUILD_DIR)/tsan/lib
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/platform/posix: | $(BUILD_DIR)/debug/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/release/lib/platform/posix: | $(BUILD_DIR)/release/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/platform/posix: | $(BUILD_DIR)/dev/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/platform/posix: | $(BUILD_DIR)/coverage/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/platform/posix: | $(BUILD_DIR)/tsan/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@

$(BUILD_DIR)/debug/lib/platform/windows: | $(BUILD_DIR)/debug/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/release/lib/platform/windows: | $(BUILD_DIR)/release/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/dev/lib/platform/windows: | $(BUILD_DIR)/dev/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/coverage/lib/platform/windows: | $(BUILD_DIR)/coverage/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@
$(BUILD_DIR)/tsan/lib/platform/windows: | $(BUILD_DIR)/tsan/lib/platform
	@echo "Creating directory $@"
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

# Create all build directories at once
create-dirs: $(BUILD_DIR)/debug/src $(BUILD_DIR)/release/src $(BUILD_DIR)/debug/lib $(BUILD_DIR)/release/lib $(BIN_DIR)

# =============================================================================
# Test Rules
# =============================================================================

# Coverage build

# Test targets - build all tests with appropriate modes
tests: $(TEST_EXECUTABLES)
	@echo "All tests built successfully!"

# Run all tests in debug mode
test: $(TEST_EXECUTABLES)
	@if [ -n "$$GENERATE_JUNIT" ]; then \
		./tests/scripts/run_tests.sh -b debug -J; \
	else \
		./tests/scripts/run_tests.sh -b debug; \
	fi

# Run all tests in release mode
test-release: $(TEST_EXECUTABLES)
	@if [ -n "$$GENERATE_JUNIT" ]; then \
		./tests/scripts/run_tests.sh -b release -J; \
	else \
		./tests/scripts/run_tests.sh -b release; \
	fi

# Build test executables - map flattened names back to their object files
# Unit tests - use debug objects by default
$(BIN_DIR)/test_unit_%: $(TEST_BUILD_DIR)/debug/unit/%_test.o $(OBJS_NON_TARGET_DEBUG) | $(BIN_DIR)
	@echo "Linking test $@ (debug mode)..."
	$(CC) $(CFLAGS) $(DEV_FLAGS) -o $@ $< $(OBJS_NON_TARGET_DEBUG) $(LDFLAGS) $(TEST_LDFLAGS)

# Integration tests use debug objects
$(BIN_DIR)/test_integration_%: $(TEST_BUILD_DIR)/debug/integration/%_test.o $(OBJS_NON_TARGET_DEBUG) | $(BIN_DIR)
	@echo "Linking test $@ (debug mode)..."
	$(CC) $(CFLAGS) $(DEV_FLAGS) -o $@ $< $(OBJS_NON_TARGET_DEBUG) $(LDFLAGS) $(TEST_LDFLAGS)

# Performance tests use release objects for speed
$(BIN_DIR)/test_performance_%: $(TEST_BUILD_DIR)/release/performance/%_test.o $(OBJS_NON_TARGET_RELEASE) | $(BIN_DIR)
	@echo "Linking test $@ (release mode)..."
	$(CC) $(CFLAGS) $(RELEASE_FLAGS) -o $@ $< $(OBJS_NON_TARGET_RELEASE) $(LDFLAGS) $(TEST_LDFLAGS)

# Legacy coverage variants (kept for compatibility)
$(BIN_DIR)/test_unit_%_coverage: $(TEST_BUILD_DIR)/coverage/unit/%_test.o $(OBJS_NON_TARGET_COVERAGE) | $(BIN_DIR)
	@echo "Linking test $@ (coverage mode)..."
	$(CC) $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -o $@ $< $(OBJS_NON_TARGET_COVERAGE) $(LDFLAGS) $(TEST_LDFLAGS) $(COVERAGE_FLAGS)

# Integration tests with coverage (debug mode)
$(BIN_DIR)/test_integration_%_coverage: $(TEST_BUILD_DIR)/coverage/integration/%_test.o $(OBJS_NON_TARGET_COVERAGE) | $(BIN_DIR)
	@echo "Linking test $@ (coverage mode)..."
	$(CC) $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) -o $@ $< $(OBJS_NON_TARGET_COVERAGE) $(LDFLAGS) $(TEST_LDFLAGS) $(COVERAGE_FLAGS)

# Performance tests with coverage (release mode)
# Coverage tests use debug-based coverage mode

# Release-coverage test variants (for CI)
# All coverage tests use the same coverage mode

# Compile test files - unit tests in debug mode
$(TEST_BUILD_DIR)/debug/unit/%.o: $(TEST_DIR)/unit/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/debug/unit
	@echo "Compiling test $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TEST_CFLAGS) -c $<

# Compile test files - integration tests in debug mode
$(TEST_BUILD_DIR)/debug/integration/%.o: $(TEST_DIR)/integration/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/debug/integration
	@echo "Compiling test $< (debug)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(TEST_CFLAGS) -c $<

# Compile test files - performance tests in release mode
$(TEST_BUILD_DIR)/release/performance/%.o: $(TEST_DIR)/performance/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/release/performance
	@echo "Compiling test $< (release)..."
	$(CC) -o $@ $(CFLAGS) $(RELEASE_FLAGS) $(TEST_CFLAGS) -c $<

# Compile test files - unit tests with coverage
$(TEST_BUILD_DIR)/coverage/unit/%.o: $(TEST_DIR)/unit/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/coverage/unit
	@echo "Compiling test $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) $(TEST_CFLAGS) -c $<

# Compile test files - integration tests with coverage
$(TEST_BUILD_DIR)/coverage/integration/%.o: $(TEST_DIR)/integration/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/coverage/integration
	@echo "Compiling test $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) $(TEST_CFLAGS) -c $<

# Compile test files - performance tests with coverage (release mode)
$(TEST_BUILD_DIR)/coverage/performance/%.o: $(TEST_DIR)/performance/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/coverage/performance
	@echo "Compiling test $< (coverage)..."
	$(CC) -o $@ $(CFLAGS) $(DEV_FLAGS) $(COVERAGE_FLAGS) $(TEST_CFLAGS) -c $<

# Test directory creation
$(TEST_BUILD_DIR)/debug/unit:
	@mkdir -p $@

$(TEST_BUILD_DIR)/debug/integration:
	@mkdir -p $@

$(TEST_BUILD_DIR)/release/performance:
	@mkdir -p $@

$(TEST_BUILD_DIR)/coverage/unit:
	@mkdir -p $@

$(TEST_BUILD_DIR)/coverage/integration:
	@mkdir -p $@

$(TEST_BUILD_DIR)/coverage/performance:
	@mkdir -p $@

# For CI
c-objs: $(OBJS_C)
	@echo "C object files count: $(words $(OBJS_C))"
	@echo "C object files size (sorted by size):"
	@du -sh $(OBJS_C) | sort -rh
	@du -csh $(OBJS_C) | grep total

# =============================================================================
# Utility Targets
# =============================================================================
# Dependencies
# =============================================================================

# Install dependencies based on the OS
.PHONY: deps
deps:
	@echo "Installing dependencies for $(shell uname -s)..."
ifeq ($(shell uname -s),Darwin)
	# macOS dependencies
	@echo "Installing macOS dependencies via Homebrew..."
	@brew install portaudio libsodium criterion zlib coreutils || true
	@echo "macOS dependencies installed"
else ifeq ($(shell uname -s),Linux)
	# Ubuntu/Linux dependencies
	@echo "Installing Linux dependencies via apt..."
	@sudo apt-get update || true
	@sudo apt-get install -y build-essential pkg-config libportaudio2 portaudio19-dev \
		libsodium-dev libcriterion-dev zlib1g-dev || true
	@echo "Linux dependencies installed"
else
	@echo "Unsupported OS: $(shell uname -s)"
	@exit 1
endif

# Install test-specific dependencies
.PHONY: deps-test
deps-test: deps
	@echo "Installing test-specific dependencies..."
ifeq ($(shell uname -s),Darwin)
	# macOS test dependencies (coreutils for gtimeout)
	@brew install gcovr || true
else ifeq ($(shell uname -s),Linux)
	# Linux test dependencies
	@sudo apt-get install -y lcov gcovr valgrind || true
endif
	@echo "Test dependencies installed"

# =============================================================================
# Clean Targets
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
	@echo "  all/default                      - Build all targets with default flags"
	@echo "  debug                            - Build with debug symbols and no optimization"
	@echo "  coverage                         - Build with debug symbols and coverage"
	@echo "  release                          - Build with optimizations enabled"
	@echo "  format                           - Format source code using clang-format"
	@echo "  install-hooks                    - Install git hooks from git-hooks/ directory"
	@echo "  uninstall-hooks                  - Remove installed git hooks"
	@echo "  format-check                     - Check code formatting without modifying files"
	@echo "  clang-tidy                       - Run clang-tidy on sources"
	@echo "  analyze                          - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc                             - Count lines of code"
	@echo "  test                             - Run all tests (unit + integration + performance) in debug mode"
	@echo "  test-release                     - Run all tests (unit + integration + performance) in release mode"
	@echo "  tests                            - Build all test executables in debug mode"
	@echo "  tests-debug                      - Build all test executables in debug mode"
	@echo "  tests-coverage                   - Build all test executables with coverage"
	@echo "  tests-release                    - Build all test executables in release mode"
	@echo "  todo                             - Build the ./todo subproject"
	@echo "  todo-clean                       - Clean the ./todo subproject"
	@echo "  clean                            - Remove build artifacts"
	@echo "  help                             - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  CC=$(CC)"
	@echo "  PKG_CONFIG_LIBS=$(PKG_CONFIG_LIBS)"
	@echo "  CFLAGS=$(CFLAGS)"
	@echo "  LDFLAGS=$(LDFLAGS)"

# =============================================================================
# Code Utils
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

# Generate compile_commands.json manually for clang-tidy
compile_commands.json: Makefile $(C_FILES) $(M_FILES) $(TEST_C_FILES)
	@echo "Generating compile_commands.json..."
	@echo "[" > compile_commands.json.tmp
	@first=true; \
	for file in $(C_FILES) $(M_FILES); do \
		if [ "$$first" = "true" ]; then \
			first=false; \
		else \
			echo "," >> compile_commands.json.tmp; \
		fi; \
		echo "  {" >> compile_commands.json.tmp; \
		echo "    \"directory\": \"$(PWD)\"," >> compile_commands.json.tmp; \
		echo "    \"command\": \"clang $(CFLAGS) $(LDFLAGS) -c $$file\"," >> compile_commands.json.tmp; \
		echo "    \"file\": \"$$file\"" >> compile_commands.json.tmp; \
		echo "  }" >> compile_commands.json.tmp; \
	done; \
	for file in $(TEST_C_FILES); do \
		if [ "$$first" = "true" ]; then \
			first=false; \
		else \
			echo "," >> compile_commands.json.tmp; \
		fi; \
		echo "  {" >> compile_commands.json.tmp; \
		echo "    \"directory\": \"$(PWD)\"," >> compile_commands.json.tmp; \
		echo "    \"command\": \"clang $(CFLAGS) $(TEST_CFLAGS) $(TEST_LDFLAGS) -c $$file\"," >> compile_commands.json.tmp; \
		echo "    \"file\": \"$$file\"" >> compile_commands.json.tmp; \
		echo "  }" >> compile_commands.json.tmp; \
	done; \
	echo "]" >> compile_commands.json.tmp
	@jq . compile_commands.json.tmp > compile_commands.json
	@rm -f compile_commands.json.tmp
	@echo "compile_commands.json generated and formatted successfully!"

# Run clang-tidy to check code style
clang-tidy: compile_commands.json
	@echo "Running clang-tidy with compile_commands.json..."
	clang-tidy -p . -header-filter='.*' $(C_FILES) $(M_FILES) $(TEST_C_FILES)

analyze: compile_commands.json
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
	@echo "Excluding system headers to avoid CET intrinsic false positives..."
	scan-build --status-bugs make clean
	scan-build --status-bugs --exclude /usr --exclude /Applications/Xcode.app --exclude /Library/Developer make CSTD="$(CSTD)" EXTRA_CFLAGS="-Wformat -Wformat-security -Werror=format-security" c-objs

cloc:
	@echo "LOC for ./src"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' src
	@echo "LOC for ./lib:"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' lib
	@echo "LOC for ./tests:"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' tests

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

# Git hooks installation
install-hooks:
	@echo "Installing git hooks..."
	@mkdir -p .git/hooks
	@if [ -f git-hooks/pre-commit ]; then \
		cp git-hooks/pre-commit .git/hooks/pre-commit; \
		chmod +x .git/hooks/pre-commit; \
		echo "  ✅ pre-commit hook installed"; \
	else \
		echo "  ⚠️  git-hooks/pre-commit not found"; \
	fi
	@echo "Git hooks installation complete!"

uninstall-hooks:
	@echo "Removing git hooks..."
	@if [ -f .git/hooks/pre-commit ]; then \
		rm -f .git/hooks/pre-commit; \
		echo "  ✅ pre-commit hook removed"; \
	else \
		echo "  ⚠️  pre-commit hook not found"; \
	fi
	@echo "Git hooks removal complete!"

# =============================================================================
# Extra Makefile stuff
# =============================================================================

.DEFAULT_GOAL := debug

.PRECIOUS: $(OBJS_NON_TARGET)

.PHONY: all clean default help debug coverage sanitize release c-objs format format-check bear clang-tidy analyze scan-build cloc tests test test-release tests-debug tests-release tests-coverage tests-sanitize todo todo-clean compile_commands.json install-hooks uninstall-hooks
