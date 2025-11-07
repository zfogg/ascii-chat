#!/usr/bin/make -f


# =============================================================================
# Configuration
# =============================================================================

SOURCE_DIR := $(abspath .)
BUILD_DIR ?= $(SOURCE_DIR)/build
BIN_DIR   := $(SOURCE_DIR)/bin
SRC_DIR   := $(SOURCE_DIR)/src
LIB_DIR   := $(SOURCE_DIR)/lib
DOCS_DIR  := $(SOURCE_DIR)/docs

CMAKE ?= cmake
CMAKE_GENERATOR ?= Ninja
CMAKE_BUILD_TYPE ?= RelWithDebInfo
CMAKE_ARGS ?=
EXTRA_CMAKE_ARGS ?=

# Compiler configuration helpers used by analysis targets
CC := clang
override CFLAGS += -I$(LIB_DIR) -I$(SRC_DIR)


# =============================================================================
# CMake driver targets
# =============================================================================

ifeq ($(wildcard $(BUILD_DIR)/CMakeCache.txt),)
CMAKE_GENERATOR_ARG := -G "$(CMAKE_GENERATOR)"
else
CMAKE_GENERATOR_ARG :=
endif

CONFIGURE_ARGS := $(CMAKE_GENERATOR_ARG) -S "$(SOURCE_DIR)" -B "$(BUILD_DIR)"

ifneq ($(strip $(CMAKE_BUILD_TYPE)),)
CONFIGURE_ARGS += -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE)
endif

ifneq ($(strip $(CMAKE_ARGS)),)
CONFIGURE_ARGS += $(CMAKE_ARGS)
endif

ifneq ($(strip $(EXTRA_CMAKE_ARGS)),)
CONFIGURE_ARGS += $(EXTRA_CMAKE_ARGS)
endif

.DEFAULT_GOAL := build

.PHONY: all build configure reconfigure clean distclean install ninja

all: build

configure:
	@echo "Configuring CMake (build type: $(CMAKE_BUILD_TYPE))..."
	@env MAKEFLAGS= $(CMAKE) $(CONFIGURE_ARGS)

build: configure
	@echo "Building ascii-chat..."
	@env MAKEFLAGS= $(CMAKE) --build "$(BUILD_DIR)"

reconfigure:
	@echo "Forcing a fresh CMake configure..."
	@rm -f "$(BUILD_DIR)/CMakeCache.txt"
	@$(MAKE) configure

clean:
	@if [ -d "$(BUILD_DIR)" ]; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	else \
		echo "Nothing to clean (build directory '$(BUILD_DIR)' not found)"; \
	fi

distclean:
	@echo "Removing build directory '$(BUILD_DIR)'..."
	@rm -rf "$(BUILD_DIR)"

install: build
	@$(CMAKE) --build "$(BUILD_DIR)" --target install

ninja: build
	@cd "$(BUILD_DIR)" && ninja $(NINJA_ARGS)


# =============================================================================
# File Discovery (for tooling targets)
# =============================================================================

C_FILES := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)
C_HEADERS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(LIB_DIR)/*.h)


# =============================================================================
# Helper targets
# =============================================================================

.PHONY: help format format-check clang-tidy analyze cloc

help:
	@echo "Available targets:"
	@echo "  build           - Configure (if needed) and build using CMake"
	@echo "  configure       - Run the CMake configure step"
	@echo "  reconfigure     - Remove the cache and re-run configure"
	@echo "  clean           - Invoke the generator's clean target"
	@echo "  distclean       - Remove the entire build directory"
	@echo "  install         - Build and install using CMake"
	@echo "  ninja           - Run ninja in the build directory"
	@echo "  format          - Format source code using clang-format"
	@echo "  format-check    - Check code formatting without modifying files"
	@echo "  clang-tidy      - Run clang-tidy on sources"
	@echo "  analyze         - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc            - Count lines of code"
	@echo "  help            - Show this help message"

format:
	@echo "Formatting source code..."
	@find "$(SRC_DIR)" "$(LIB_DIR)" -name "*.c" -o -name "*.h" | \
		xargs clang-format --Werror -i
	@echo "Code formatting complete!"

format-check:
	@echo "Checking code formatting..."
	@find "$(SRC_DIR)" "$(LIB_DIR)" -name "*.c" -o -name "*.h" | \
		xargs clang-format --dry-run --Werror

clang-tidy: build compile_commands.json
	@echo "Verifying clang-tidy configuration..."
	@clang-tidy --verify-config --config-file=.clang-tidy
	@echo "Running clang-tidy with compile_commands.json..."
	@clang-tidy -p . -header-filter='.*' $(C_FILES) $(M_FILES) -- $(CFLAGS)

analyze:
	@echo "Running clang static analysis (C sources)..."
	@clang --analyze $(CFLAGS) $(C_FILES)
	@echo "Running clang static analysis (Objective-C sources)..."
	@clang --analyze $(OBJCFLAGS) $(M_FILES)
	@echo "Running cppcheck..."
	@cppcheck --enable=all --inline-suppr \
		-I$(LIB_DIR) -I$(SRC_DIR) $(shell pkg-config --cflags-only-I $(PKG_CONFIG_LIBS)) \
		--suppress=missingIncludeSystem \
		$(C_FILES) $(C_HEADERS)

cloc:
	@echo "src/ dir:"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' "$(SRC_DIR)"
	@echo "lib/ dir:"
	@cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' "$(LIB_DIR)"
	@echo "docs/ dir:"
	@cloc --progress=1 --force-lang='Markdown,dox' --force-lang='XML,in' "$(DOCS_DIR)"


# =============================================================================
# Extra Makefile stuff
# =============================================================================

.PHONY: compile_commands.json

compile_commands.json: build
	@if [ -f "$(BUILD_DIR)/compile_commands.json" ]; then \
		ln -sf "$(BUILD_DIR)/compile_commands.json" "$@"; \
	else \
		echo "CMake did not generate compile_commands.json (is CMAKE_EXPORT_COMPILE_COMMANDS enabled?)"; \
	fi
