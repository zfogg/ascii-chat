#!/usr/bin/make -f


# =============================================================================
# Configuration
# =============================================================================

# Compilers
CC := clang

# Directories
BIN_DIR   := bin
BUILD_DIR := build
SRC_DIR   := src
LIB_DIR   := lib

override CFLAGS += -I$(LIB_DIR) -I$(SRC_DIR)


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

# =============================================================================
# Helper targets
# =============================================================================
# Show help information
help:
	@echo "Available targets:"
	@echo "  format          - Format source code using clang-format"
	@echo "  format-check    - Check code formatting without modifying files"
	@echo "  clang-tidy      - Run clang-tidy on sources"
	@echo "  analyze         - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc            - Count lines of code"
	@echo "  help            - Show this help message"

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

cloc:
	@echo "src/ dir:"
	cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' src
	@echo "lib/ dir:"
	cloc --progress=1 --include-lang='C,C/C++ Header,Objective-C' lib
	@echo "docs/ dir:"
	cloc --progress=1 --force-lang='Markdown,dox' --force-lang='XML,in' docs

# =============================================================================
# Extra Makefile stuff
# =============================================================================

.DEFAULT_GOAL := debug

.PHONY: all help format format-check clang-tidy analyze cloc
