#!/usr/bin/make -f

# =============================================================================
# Configuration
# =============================================================================

# Compilers
CC  := clang
CXX := clang++

# Package dependencies
PKG_CONFIG_LIBS := opencv4 libjpeg

# Directories
BIN_DIR  := bin
BUILD_DIR := build

# =============================================================================
# Compiler Flags
# =============================================================================

# Base flags
CFLAGS   += -Wall -Wextra
CXXFLAGS += $(CFLAGS)

# Get package-specific flags
PKG_CFLAGS := $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))

# Apply package flags and language flags
CFLAGS   +=  $(PKG_CFLAGS) -std=c23
CXXFLAGS +=  $(PKG_CFLAGS) -std=c++23 -stdlib=libc++

# =============================================================================
# Linker Flags
# =============================================================================

LDFLAGS += $(shell pkg-config --libs --static $(PKG_CONFIG_LIBS))

# =============================================================================
# File Discovery
# =============================================================================

# Targets (executables)
TARGETS := $(addprefix $(BIN_DIR)/, server client)

# Object files for server and client
OBJS_C    := $(patsubst %.c,   $(BUILD_DIR)/%.o, $(wildcard *.c))
OBJS_CPP  := $(patsubst %.cpp, $(BUILD_DIR)/%.o, $(wildcard *.cpp))
OBJS_CEXT := $(patsubst %.c,   $(BUILD_DIR)/%.o, $(wildcard $(addprefix ext/, $(EXT_CDEPS))/*.c))

# All object files for server and client
OBJS := $(OBJS_C) $(OBJS_CPP) $(OBJS_CEXT)

# Non-target object files (files without main methods)
OBJS_NON_TARGET := $(filter-out $(patsubst $(BIN_DIR)/%, $(BUILD_DIR)/%.o, $(TARGETS)), $(OBJS))

# Header files
HEADERS_C    := $(wildcard *.h)
HEADERS_CPP  := $(wildcard *.hpp)
HEADERS_CEXT := $(wildcard $(addprefix ext/, $(EXT_CDEPS))/*.h)
HEADERS      := $(HEADERS_C) $(HEADERS_CPP) $(HEADERS_CEXT)

# =============================================================================
# Phony Targets
# =============================================================================

.PHONY: all clean default help

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

# Build executables
$(BIN_DIR)/server: $(BUILD_DIR)/server.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Built $@ successfully!"

$(BIN_DIR)/client: $(BUILD_DIR)/client.o $(OBJS_NON_TARGET)
	@echo "Linking $@..."
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

# Compile external C source files
$(OBJS_CEXT): $(BUILD_DIR)/%.o: %.c $(HEADERS_CEXT)
	@echo "Compiling external $<..."
	@mkdir -p $(dir $@)
	$(CC) -o $@ $(CFLAGS) -c $<

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
	@echo "  all     - Build all targets (default)"
	@echo "  default - Build all targets"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Configuration:"
	@echo "  CC=$(CC)"
	@echo "  CXX=$(CXX)"
	@echo "  BIN_DIR=$(BIN_DIR)"
	@echo "  BUILD_DIR=$(BUILD_DIR)"
	@echo "  PKG_CONFIG_LIBS=$(PKG_CONFIG_LIBS)"

# =============================================================================
# Dependencies
# =============================================================================

.PRECIOUS: $(OBJS_NON_TARGET)
