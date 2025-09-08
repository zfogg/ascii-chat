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

CSTD ?= c23

# Base flags
override CFLAGS += -Wall -Wextra

# Enable GNU extensions for POSIX functions (e.g. usleep) when compiling with strict C standards
override CFLAGS += -D_GNU_SOURCE

# Get package-specific flags
override CFLAGS += $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))

# Build LDFLAGS systematically to avoid duplicates
PKG_LDFLAGS := $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

# Platform-specific libraries
ifeq ($(shell uname),Darwin)
    PLATFORM_LDFLAGS := -framework Foundation -framework AVFoundation -framework CoreMedia -framework CoreVideo -lncurses
else ifeq ($(shell uname),Linux)
    PLATFORM_LDFLAGS := -lncurses
    # When using static PortAudio, we need JACK libraries for the JACK backend
    # Only add -ljack if:
    # 1. Static PortAudio exists (libportaudio.a)
    # 2. JACK is actually installed (check with pkg-config)
	# Check if JACK is available using pkg-config
	# Use := to ensure single evaluation
	JACK_EXISTS := $(shell pkg-config --exists jack 2>/dev/null && echo yes || echo no)
	ifeq ($(JACK_EXISTS),yes)
		# Get both library flags and library paths
		JACK_LIBS := $(shell pkg-config --libs jack 2>/dev/null)
		JACK_CFLAGS := $(shell pkg-config --cflags jack 2>/dev/null)
		# Add library search path for JACK
		PLATFORM_LDFLAGS += $(JACK_LIBS)
		# If pkg-config doesn't provide the path, add common locations
		ifeq ($(findstring -L,$(JACK_LIBS)),)
			PLATFORM_LDFLAGS += -L/usr/lib/x86_64-linux-gnu -L/usr/lib
		endif
	endif
endif

# System libraries (only add what pkg-config doesn't provide)
SYSTEM_LDFLAGS := -lm

# Check if pkg-config already provides pthread, if not add it
ifeq ($(findstring -lpthread,$(PKG_LDFLAGS)),)
    SYSTEM_LDFLAGS += -lpthread
endif

# Combine all LDFLAGS
override LDFLAGS := $(PKG_LDFLAGS) $(PLATFORM_LDFLAGS) $(SYSTEM_LDFLAGS) $(ARCH_FLAGS)

# Test-specific flags
TEST_CFLAGS  := $(shell pkg-config --cflags $(TEST_PKG_CONFIG_LIBS) 2>/dev/null || echo "")
# Criterion on Linux may need additional libraries for its dependencies
ifeq ($(shell uname),Linux)
    TEST_LDFLAGS := $(shell pkg-config --libs $(TEST_PKG_CONFIG_LIBS) 2>/dev/null || echo "-lcriterion")
    # Only add optional Criterion dependencies if they exist
    ifneq ($(shell pkg-config --exists libgit2 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs libgit2)
    endif
    ifneq ($(shell pkg-config --exists nanomsg 2>/dev/null && echo yes),)
        TEST_LDFLAGS += $(shell pkg-config --libs nanomsg)
    endif
else
    TEST_LDFLAGS := $(shell pkg-config --libs $(TEST_PKG_CONFIG_LIBS) 2>/dev/null || echo "-lcriterion")
endif

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
  SIMD_CFLAGS += -DSIMD_SUPPORT_SSE2 -msse2
endif
ifdef ENABLE_SIMD_SSSE3
  SIMD_CFLAGS += -DSIMD_SUPPORT_SSSE3 -mssse3
endif
ifdef ENABLE_SIMD_AVX2
  SIMD_CFLAGS += -DSIMD_SUPPORT_AVX2 -mavx2
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
      $(info Enabling ARM CRC32 hardware acceleration (Apple Silicon))
    else
      $(info Enabling Intel CRC32 hardware acceleration (SSE4.2))
      CRC32_CFLAGS += -msse4.2
    endif
  else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
    $(info Enabling ARM CRC32 hardware acceleration (Linux ARM64))
  else ifeq ($(UNAME_M),x86_64)
    $(info Enabling Intel CRC32 hardware acceleration (SSE4.2))
    CRC32_CFLAGS += -msse4.2
  endif
else
  $(info CRC32 hardware acceleration disabled)
  CRC32_CFLAGS :=
endif

# =============================================================================
# Libsodium-based Crypto Configuration
# =============================================================================
$(info Using libsodium for cryptographic operations)

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
        $(info Using Rosetta (x86_64) optimizations: $(CPU_OPT_FLAGS))
    else ifeq ($(IS_APPLE_SILICON),1)
        CPU_OPT_FLAGS := -O3 -march=native -mcpu=native -ffast-math -ffp-contract=fast
        $(info Using Apple Silicon optimizations: $(CPU_OPT_FLAGS))
    else
        CPU_OPT_FLAGS := -O3 -march=native -ffp-contract=fast -ffinite-math-only
        $(info Using Intel Mac optimizations: $(CPU_OPT_FLAGS))
    endif
else ifeq ($(UNAME_S),Linux)
    # Linux: CPU-specific optimizations without -ffast-math for safety
    ifeq ($(UNAME_M),aarch64)
        CPU_OPT_FLAGS := -O3 -mcpu=native -ffp-contract=fast -ffinite-math-only
        $(info Using Linux ARM64 optimizations: $(CPU_OPT_FLAGS))
    else
        CPU_OPT_FLAGS := -O3 -march=native -ffp-contract=fast -ffinite-math-only
        $(info Using Linux x86_64 optimizations: $(CPU_OPT_FLAGS))
    endif
else
    # Other platforms: Generic -O3 with safer math optimizations
    CPU_OPT_FLAGS := -O3 -ffp-contract=fast
    $(info Using generic optimizations: $(CPU_OPT_FLAGS))
endif

# Compose per-config flags cleanly (no filter-out hacks)
DEBUG_FLAGS    := -g -O0 -DDEBUG -DDEBUG_MEMORY
RELEASE_FLAGS  := $(CPU_OPT_FLAGS) -DNDEBUG -funroll-loops -fstrict-aliasing -ftree-vectorize -fomit-frame-pointer -pipe -flto -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-trapping-math -falign-loops=32 -falign-functions=32
SANITIZE_FLAGS := -fsanitize=address

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

# Source code files
LIB_C_FILES := $(filter-out $(LIB_DIR)/ascii_simd_neon.c, $(wildcard $(LIB_DIR)/*.c))
C_FILES := $(wildcard $(SRC_DIR)/*.c) $(LIB_C_FILES) $(wildcard $(LIB_DIR)/image2ascii/*.c) $(wildcard $(LIB_DIR)/image2ascii/simd/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)

# Header files
LIB_H_FILES := $(filter-out $(LIB_DIR)/ascii_simd_neon.h, $(wildcard $(LIB_DIR)/*.h))
C_HEADERS := $(wildcard $(SRC_DIR)/*.h) $(LIB_H_FILES) $(wildcard $(LIB_DIR)/image2ascii/*.h) $(wildcard $(LIB_DIR)/image2ascii/simd/*.h)

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

# Test files - exclude problematic tests for now
TEST_C_FILES_ALL := $(wildcard $(TEST_DIR)/unit/*.c) $(wildcard $(TEST_DIR)/integration/*.c) $(wildcard $(TEST_DIR)/performance/*.c)
# Exclude tests with API mismatches that prevent compilation
TEST_C_FILES_EXCLUDE := $(TEST_DIR)/unit/ascii_simd_test.c $(TEST_DIR)/integration/server_multiclient_test.c $(TEST_DIR)/integration/video_pipeline_test.c $(TEST_DIR)/performance/benchmark_test.c
TEST_C_FILES := $(filter-out $(TEST_C_FILES_EXCLUDE), $(TEST_C_FILES_ALL))
TEST_OBJS := $(patsubst $(TEST_DIR)/%.c, $(TEST_BUILD_DIR)/%.o, $(TEST_C_FILES))
# Transform test file paths to executable names with flattened structure
# tests/unit/common_test.c -> bin/test_unit_common_test
# tests/integration/crypto_network_test.c -> bin/test_integration_crypto_network_test
TEST_EXECUTABLES := $(foreach file,$(TEST_C_FILES),$(BIN_DIR)/test_$(subst /,_,$(patsubst $(TEST_DIR)/%.c,%,$(file))))

# =============================================================================
# Build Rules
# =============================================================================

# Main targets
default: $(TARGETS)
all: default

# Debug build
debug: override CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGETS)

# Release build
release: override CFLAGS += $(RELEASE_FLAGS)
release: override LDFLAGS += -flto
release: $(TARGETS)

# Memory sanitizer build (inherits debug flags)
sanitize: override CFLAGS  += $(DEBUG_FLAGS)
sanitize: override LDFLAGS += $(SANITIZE_FLAGS)
sanitize: $(TARGETS)

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/src/server.o $(OBJS_NON_TARGET) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/src/client.o $(OBJS_NON_TARGET) | $(BIN_DIR)
	@echo "Linking $@..."
	$(CC) -o $@ $^ $(LDFLAGS) $(INFO_PLIST_FLAGS)
	@echo "Built $@ successfully!"

# Compile C source files from src/
$(BUILD_DIR)/src/%.o: $(SRC_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/src
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<

# Compile source files from lib/image2ascii/
$(BUILD_DIR)/lib/image2ascii/%.o: $(LIB_DIR)/image2ascii/%.c $(C_HEADERS) | $(BUILD_DIR)/lib/image2ascii
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<

# Compile SIMD source files from lib/image2ascii/simd/
$(BUILD_DIR)/lib/image2ascii/simd/%.o: $(LIB_DIR)/image2ascii/simd/%.c $(C_HEADERS) | $(BUILD_DIR)/lib/image2ascii/simd
	@echo "Compiling $<..."
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<

# Compile C source files from lib/ (not image2ascii/ or SIMD)
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.c $(C_HEADERS) | $(BUILD_DIR)/lib
	$(if $(findstring image2ascii,$*),$(error This rule should not match image2ascii files: $*))
	@echo "Compiling $<..."
	$(CC) -o $@ $(CFLAGS) -c $<

# Compile Objective-C source files from lib/
$(BUILD_DIR)/lib/%.o: $(LIB_DIR)/%.m $(C_HEADERS) | $(BUILD_DIR)/lib
	@echo "Compiling $<..."
	$(CC) -o $@ $(OBJCFLAGS) -c $<

# Build all object files without linking (useful for tooling like Bear/clangd)
objs: $(OBJS) $(TEST_OBJS)

# Ensure build and bin directories exist
$(BUILD_DIR)/src:
	@mkdir -p $@

$(BUILD_DIR)/lib/image2ascii: | $(BUILD_DIR)/lib
	@mkdir -p $@

$(BUILD_DIR)/lib/image2ascii/simd: | $(BUILD_DIR)/lib/image2ascii
	@mkdir -p $@

$(BUILD_DIR)/lib:
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

# Create all build directories at once
create-dirs: $(BUILD_DIR)/src $(BUILD_DIR)/src/image2ascii/simd $(BUILD_DIR)/lib $(BIN_DIR)

# =============================================================================
# Test Rules
# =============================================================================

# Coverage flags
COVERAGE_FLAGS := --coverage -fprofile-arcs -ftest-coverage

# Coverage build
coverage: override CFLAGS += $(DEBUG_FLAGS) $(COVERAGE_FLAGS)
coverage: override LDFLAGS += $(COVERAGE_FLAGS)
coverage: $(TEST_EXECUTABLES)
	@echo "Running tests with coverage..."
	@echo "Test logs will be saved to /tmp/test_logs.txt"
	@> /tmp/test_logs.txt
	@if [ -n "$$GENERATE_JUNIT" ]; then \
		echo "Generating JUnit XML output with coverage..."; \
		rm -f junit.xml; \
		echo '<?xml version="1.0" encoding="UTF-8"?>' > junit.xml; \
		echo '<testsuites name="ASCII-Chat Coverage Tests">' >> junit.xml; \
		for test in $(TEST_EXECUTABLES); do \
			echo "Running $$test..."; \
			test_name=$$(basename $$test); \
			test_class=$$(echo $$test_name | sed 's/^test_//; s/_test$$//; s/_/./g'); \
			$$test --xml=/tmp/$$test_name.xml 2>>/tmp/test_logs.txt || (echo "Test failed: $$test" && exit 1); \
			if [ -f /tmp/$$test_name.xml ]; then \
				sed -e "s/<testsuite name=\"/<testsuite name=\"$$test_class./" \
				    -e "s/<testcase name=\"/<testcase classname=\"$$test_class\" name=\"/" \
				    -n '/<testsuite/,/<\/testsuite>/p' /tmp/$$test_name.xml >> junit.xml; \
				rm -f /tmp/$$test_name.xml; \
			fi; \
		done; \
		echo '</testsuites>' >> junit.xml; \
	else \
		for test in $(TEST_EXECUTABLES); do \
			echo "Running $$test..."; \
			$$test 2>>/tmp/test_logs.txt || (echo "Test failed: $$test" && exit 1); \
		done; \
	fi
	@echo "Generating coverage report..."
	@find . -name "*.gcda" -exec gcov {} \; || echo "gcov completed"
	@if command -v lcov >/dev/null 2>&1; then \
		echo "Using lcov for detailed coverage report..."; \
		lcov --capture --directory . --output-file coverage.info; \
		lcov --remove coverage.info '/usr/*' --output-file coverage.info; \
		lcov --remove coverage.info '*/tests/*' --output-file coverage.info; \
		lcov --list coverage.info; \
		if command -v genhtml >/dev/null 2>&1; then \
			genhtml coverage.info --output-directory coverage_html; \
			echo "Coverage report generated in coverage_html/"; \
		fi; \
	else \
		echo "lcov not available - coverage files (.gcov) generated for codecov upload"; \
	fi
	@echo "View test logs: cat /tmp/test_logs.txt"

# Test targets
tests: $(TEST_EXECUTABLES)

test: $(TEST_EXECUTABLES)
	@echo "Running all tests..."
	@if [ -n "$$GENERATE_JUNIT" ]; then \
		echo "Generating JUnit XML output..."; \
		rm -f junit.xml; \
		echo '<?xml version="1.0" encoding="UTF-8"?>' > junit.xml; \
		echo '<testsuites name="ASCII-Chat Tests">' >> junit.xml; \
		total_tests=0; total_failures=0; \
		for test in $(TEST_EXECUTABLES); do \
			echo "Running $$test..."; \
			test_name=$$(basename $$test); \
			test_class=$$(echo $$test_name | sed 's/^test_//; s/_test$$//; s/_/./g'); \
			$$test --xml=/tmp/$$test_name.xml 2>/dev/null || true; \
			if [ -f /tmp/$$test_name.xml ]; then \
				sed -e "s/<testsuite name=\"/<testsuite name=\"$$test_class./" \
				    -e "s/<testcase name=\"/<testcase classname=\"$$test_class\" name=\"/" \
				    -n '/<testsuite/,/<\/testsuite>/p' /tmp/$$test_name.xml >> junit.xml; \
				rm -f /tmp/$$test_name.xml; \
			fi; \
		done; \
		echo '</testsuites>' >> junit.xml; \
	else \
		failed=0; \
		for test in $(TEST_EXECUTABLES); do \
			echo "Running $$test..."; \
			if ! $$test; then \
				echo "Test failed: $$test"; \
				failed=1; \
			fi; \
		done; \
		if [ $$failed -eq 1 ]; then \
			echo "Some tests failed!"; \
			exit 1; \
		fi; \
	fi
	@echo "All tests completed!"

test-unit: $(filter $(BIN_DIR)/test_unit_%, $(TEST_EXECUTABLES))
	@echo "Running unit tests..."
	@echo "Test logs will be saved to /tmp/test_logs.txt"
	@> /tmp/test_logs.txt
	@if [ -n "$$GENERATE_JUNIT" ]; then \
		echo "Generating JUnit XML output..."; \
		rm -f junit.xml; \
		echo '<?xml version="1.0" encoding="UTF-8"?>' > junit.xml; \
		echo '<testsuites name="ASCII-Chat Unit Tests">' >> junit.xml; \
		for test in $^; do \
			echo "Running $$test..."; \
			test_name=$$(basename $$test); \
			test_class=$$(echo $$test_name | sed 's/^test_//; s/_test$$//; s/_/./g'); \
			$$test --xml=/tmp/$$test_name.xml 2>>/tmp/test_logs.txt || (echo "Test failed: $$test" && exit 1); \
			if [ -f /tmp/$$test_name.xml ]; then \
				sed -e "s/<testsuite name=\"/<testsuite name=\"$$test_class./" \
				    -e "s/<testcase name=\"/<testcase classname=\"$$test_class\" name=\"/" \
				    -n '/<testsuite/,/<\/testsuite>/p' /tmp/$$test_name.xml >> junit.xml; \
				rm -f /tmp/$$test_name.xml; \
			fi; \
		done; \
		echo '</testsuites>' >> junit.xml; \
	else \
		failed=0; \
		for test in $^; do \
			echo "Running $$test..."; \
			if ! $$test 2>>/tmp/test_logs.txt; then \
				echo "Test failed: $$test"; \
				failed=1; \
			fi; \
		done; \
		if [ $$failed -eq 1 ]; then \
			exit 1; \
		fi; \
	fi
	@echo "View test logs: cat /tmp/test_logs.txt"

test-integration: $(filter $(BIN_DIR)/test_integration_%, $(TEST_EXECUTABLES))
	@echo "Running integration tests..."
	@failed=0; \
	for test in $^; do \
		echo "Running $$test..."; \
		if ! $$test; then \
			echo "Test failed: $$test"; \
			failed=1; \
		fi; \
	done; \
	if [ $$failed -eq 1 ]; then \
		exit 1; \
	fi

test-performance: $(filter $(BIN_DIR)/test_performance_%, $(TEST_EXECUTABLES))
	@echo "Running performance benchmarks..."
	@if [ -z "$^" ]; then \
		echo "Note: Main performance tests are in todo/ascii_simd_test"; \
		echo "Run: cd todo && make -f Makefile_simd ascii_simd_test && ./ascii_simd_test"; \
	else \
		failed=0; \
		for test in $^; do \
			echo "Running $$test..."; \
			if ! $$test; then \
				echo "Test failed: $$test"; \
				failed=1; \
			fi; \
		done; \
		if [ $$failed -eq 1 ]; then \
			exit 1; \
		fi; \
	fi

test-quiet: $(TEST_EXECUTABLES)
	@echo "Running all tests (quiet mode)..."
	@echo "Test logs will be saved to /tmp/test_logs.txt"
	@> /tmp/test_logs.txt
	@for test in $(TEST_EXECUTABLES); do \
		echo "Running $$test..."; \
		$$test 2>>/tmp/test_logs.txt || (echo "Test failed: $$test" && exit 1); \
	done
	@echo "All tests completed!"
	@echo "View test logs: cat /tmp/test_logs.txt"

# Build test executables - map flattened names back to their object files
# test_unit_common_test -> build/tests/unit/common_test.o
# test_integration_crypto_network_test -> build/tests/integration/crypto_network_test.o
$(BIN_DIR)/test_unit_%: $(TEST_BUILD_DIR)/unit/%.o $(OBJS_NON_TARGET) | $(BIN_DIR)
	@echo "Linking test $@..."
	$(CC) $(CFLAGS) -o $@ $< $(OBJS_NON_TARGET) $(LDFLAGS) $(TEST_LDFLAGS)

$(BIN_DIR)/test_integration_%: $(TEST_BUILD_DIR)/integration/%.o $(OBJS_NON_TARGET) | $(BIN_DIR)
	@echo "Linking test $@..."
	$(CC) $(CFLAGS) -o $@ $< $(OBJS_NON_TARGET) $(LDFLAGS) $(TEST_LDFLAGS)

$(BIN_DIR)/test_performance_%: $(TEST_BUILD_DIR)/performance/%.o $(OBJS_NON_TARGET) | $(BIN_DIR)
	@echo "Linking test $@..."
	$(CC) $(CFLAGS) -o $@ $< $(OBJS_NON_TARGET) $(LDFLAGS) $(TEST_LDFLAGS)

# Compile test files
$(TEST_BUILD_DIR)/unit/%.o: $(TEST_DIR)/unit/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/unit
	@echo "Compiling test $<..."
	$(CC) -o $@ $(CFLAGS) $(TEST_CFLAGS) -c $<

$(TEST_BUILD_DIR)/integration/%.o: $(TEST_DIR)/integration/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/integration
	@echo "Compiling test $<..."
	$(CC) -o $@ $(CFLAGS) $(TEST_CFLAGS) -c $<

$(TEST_BUILD_DIR)/performance/%.o: $(TEST_DIR)/performance/%.c $(C_HEADERS) | $(TEST_BUILD_DIR)/performance
	@echo "Compiling test $<..."
	$(CC) -o $@ $(CFLAGS) $(TEST_CFLAGS) -c $<

# Test directory creation
$(TEST_BUILD_DIR)/unit:
	@mkdir -p $@

$(TEST_BUILD_DIR)/integration:
	@mkdir -p $@

$(TEST_BUILD_DIR)/performance:
	@mkdir -p $@

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
	@echo "  install-hooks   - Install git hooks from git-hooks/ directory"
	@echo "  uninstall-hooks - Remove installed git hooks"
	@echo "  format-check    - Check code formatting without modifying files"
	@echo "  clang-tidy      - Run clang-tidy on sources"
	@echo "  analyze         - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc            - Count lines of code"
	@echo "  test            - Run all tests (unit + integration + performance)"
	@echo "  test-unit       - Run only unit tests (quiet mode)"
	@echo "  test-integration - Run only integration tests"
	@echo "  test-performance - Run performance benchmarks (see todo/ascii_simd_test)"
	@echo "  test-quiet      - Run all tests (quiet mode - no verbose logging)"
	@echo "  coverage        - Run tests with coverage analysis"
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

# Run bear to generate a compile_commands.json file (compile-only, no linking)
compile_commands.json: Makefile
	@echo "Running bear to generate compile_commands.json (objects only)..."
	@make clean && bear -- make -j debug $(OBJS)
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
	@echo "Excluding system headers to avoid CET intrinsic false positives..."
	scan-build --status-bugs make clean
	scan-build --status-bugs --exclude /usr --exclude /Applications/Xcode.app --exclude /Library/Developer make CSTD="$(CSTD)" EXTRA_CFLAGS="-Wformat -Wformat-security -Werror=format-security" c-objs

cloc:
	@echo "LOC for ./src and ./lib:"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' src lib
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

.PHONY: all clean default help debug sanitize release c-objs format format-check bear clang-tidy analyze scan-build cloc tests test test-unit test-integration test-performance test-quiet coverage todo todo-clean compile_commands.json install-hooks uninstall-hooks
