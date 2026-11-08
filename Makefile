#!/usr/bin/env make -f


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

.PHONY: sql-generate sql-check init-sql-tools ensure-mmmbed
SQL_EXEEXT := $(if $(filter Windows_NT,$(OS)),.exe,)
SQL_MMMBED := $(BIN_DIR)/mmmbed$(SQL_EXEEXT)
ifeq ($(origin MMMBED),undefined)
MMMBED := $(SQL_MMMBED)
SQL_TOOL_PREREQUISITE := ensure-mmmbed
endif
GO ?= go
SQL_CC_AUTODETECT := $(if $(filter undefined,$(origin SQL_CC)),1,0)
SQL_CC ?= $(if $(filter Windows_NT,$(OS)),$(if $(filter ARM64 CLANGARM64,$(PROCESSOR_ARCHITECTURE) $(PROCESSOR_ARCHITEW6432) $(MSYSTEM)),aarch64-w64-mingw32-clang,gcc),$(CC))
SQL_MMMBED_DIR := $(SOURCE_DIR)/deps/ascii-chat-deps/mmmbed

init-sql-tools:
	@test -f "$(SQL_MMMBED_DIR)/go.mod" || git submodule update --init --checkout -- deps/ascii-chat-deps/mmmbed
	@test -f "$(SQL_MMMBED_DIR)/go.mod" || { echo >&2 "mmmbed source is missing"; exit 1; }
	@if [ ! -e "$(SQL_MMMBED_DIR)/.git" ]; then exit 0; fi; \
	  expected=$$(git rev-parse :deps/ascii-chat-deps/mmmbed) && \
	  actual=$$(git -C "$(SQL_MMMBED_DIR)" rev-parse HEAD) && \
	  if [ "$${actual}" != "$${expected}" ]; then \
	    if [ -n "$$(git -C "$(SQL_MMMBED_DIR)" status --porcelain)" ] || \
	       git -C "$(SQL_MMMBED_DIR)" symbolic-ref --quiet HEAD >/dev/null; then \
	      echo >&2 "mmmbed submodule has local work; update it to $${expected} before generating SQL"; exit 1; \
	    fi; \
	    git submodule update --init --checkout -- deps/ascii-chat-deps/mmmbed; \
	    actual=$$(git -C "$(SQL_MMMBED_DIR)" rev-parse HEAD) && [ "$${actual}" = "$${expected}" ]; \
	  fi

ensure-mmmbed: init-sql-tools
	@mkdir -p "$(BIN_DIR)" "$(BUILD_DIR)"
	@go_command="$(GO)"; \
	  go_version=$$(sed -n 's/^go //p' "$(SQL_MMMBED_DIR)/go.mod"); \
	  go_switch=$$("$${go_command}" env GOTOOLCHAIN 2>/dev/null); \
	  installed=$$("$${go_command}" version | sed -n 's/^go version go\([0-9][0-9.]*\).*/\1/p'); \
	  automatic=0; case "$${go_switch}" in auto|path|*+auto|*+path) automatic=1;; esac; \
	  if [ -z "$${go_switch}" ] || { [ "$${automatic}" = 0 ] && \
	    awk -v have="$${installed}" -v need="$${go_version}" 'BEGIN { split(have,h,"."); split(need,n,"."); for (i=1;i<=3;i++) { if (h[i]+0<n[i]+0) exit 0; if (h[i]+0>n[i]+0) exit 1 } exit 1 }'; }; then \
	      go_bin=$$("$${go_command}" env GOBIN); \
	      if [ -z "$${go_bin}" ]; then \
	        go_bin=$$("$${go_command}" env GOPATH); \
	        $(if $(filter Windows_NT,$(OS)),go_bin="$${go_bin%%;*}",go_bin="$${go_bin%%:*}"); \
	        go_bin="$${go_bin}/bin"; \
	      fi; \
	      go_command="$${go_bin}/go$${go_version}$(SQL_EXEEXT)"; \
	      go_switch=local; \
	  fi; \
	  host_os=$$(GOTOOLCHAIN="$${go_switch}" "$${go_command}" env GOHOSTOS) && \
	    host_arch=$$(GOTOOLCHAIN="$${go_switch}" "$${go_command}" env GOHOSTARCH) || exit 1; \
	  sql_cc="$(SQL_CC)"; \
	  if [ "$(SQL_CC_AUTODETECT)" = 1 ] && [ "$${host_os}" = windows ] && [ "$${host_arch}" = arm64 ]; then \
	    sql_cc=aarch64-w64-mingw32-clang; \
	  fi; \
	  cache="$(abspath $(BUILD_DIR))/sql-tools/$${host_os}-$${host_arch}"; mkdir -p "$${cache}"; \
	  revision=""; dirty=""; \
	  if [ -e "$(SQL_MMMBED_DIR)/.git" ]; then \
	    revision=$$(git -C "$(SQL_MMMBED_DIR)" rev-parse HEAD) || exit 1; \
	    dirty=$$(git -C "$(SQL_MMMBED_DIR)" status --porcelain) || exit 1; \
	  fi; \
	  if [ -n "$${dirty}" ] || [ -z "$${revision}" ] || [ ! -f "$${cache}/mmmbed$(SQL_EXEEXT)" ] || \
	     [ "$${revision}" != "$$(cat "$${cache}/revision" 2>/dev/null)" ]; then \
	    cd "$(SQL_MMMBED_DIR)" && GOTOOLCHAIN="$${go_switch}" CGO_ENABLED=1 CC="$${sql_cc}" GOOS="$${host_os}" GOARCH="$${host_arch}" \
	      "$${go_command}" build -o "$${cache}/mmmbed$(SQL_EXEEXT)" ./cmd/mmmbed && \
	    if [ -n "$${dirty}" ]; then printf 'dirty:%s\n' "$${revision}"; else printf '%s\n' "$${revision}"; fi > "$${cache}/revision" || exit 1; \
	  fi; \
	  cp "$${cache}/mmmbed$(SQL_EXEEXT)" "$(SQL_MMMBED)"

sql-generate: $(SQL_TOOL_PREREQUISITE)
	"$(MMMBED)" sql -l c --prefix database --format --dialect sqlite -i lib/discovery/sql -i lib/network/rate_limit/sql -o lib/discovery/queries_generated.h

sql-check: $(SQL_TOOL_PREREQUISITE)
	"$(MMMBED)" sql -l c --prefix database --format --dialect sqlite -i lib/discovery/sql -i lib/network/rate_limit/sql -o lib/discovery/queries_generated.h --check

INSTALL_PREFIX_FROM_ARGS := $(strip $(shell printf '%s %s\n' "$(CMAKE_ARGS)" "$(EXTRA_CMAKE_ARGS)" | tr ' ' '\n' | grep -E '^-DCMAKE_INSTALL_PREFIX=' | tail -n1 | cut -d '=' -f2- ))

ifneq ($(strip $(INSTALL_PREFIX_FROM_ARGS)),)
  CMAKE_INSTALL_PREFIX ?= $(INSTALL_PREFIX_FROM_ARGS)
endif

# Compiler configuration helpers used by analysis targets
CC := clang
override CFLAGS += -I$(LIB_DIR) -I$(SRC_DIR)


Black   = "\e[1;30m"
Red     = "\e[1;31m"
Green   = "\e[1;32m"
Yellow  = "\e[1;33m"
Blue    = "\e[1;34m"
Purple  = "\e[1;35m"
Cyan    = "\e[1;36m"
White   = "\e[1;37m"
Reset   = "\e[0m"


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

ifneq ($(strip $(CMAKE_INSTALL_PREFIX)),)
ifneq ($(strip $(INSTALL_PREFIX_FROM_ARGS)),$(strip $(CMAKE_INSTALL_PREFIX)))
CONFIGURE_ARGS += -DCMAKE_INSTALL_PREFIX="$(CMAKE_INSTALL_PREFIX)"
endif
endif

.DEFAULT_GOAL := build

.PHONY: all build configure reconfigure clean distclean install ninja checkprefix

all: build

configure:
	@echo "Configuring CMake (build type: $(CMAKE_BUILD_TYPE))..."
	@env MAKEFLAGS= $(CMAKE) $(CONFIGURE_ARGS)

build: $(BUILD_DIR)/CMakeCache.txt
	@echo "Building ascii-chat..."
	@env MAKEFLAGS= $(CMAKE) --build "$(BUILD_DIR)"

$(BUILD_DIR)/CMakeCache.txt:
	@$(MAKE) configure

ninja: build
	@cd "$(BUILD_DIR)" && ninja $(NINJA_ARGS)

reconfigure:
	@echo "Forcing a fresh CMake configure..."
	@rm -f "$(BUILD_DIR)/CMakeCache.txt"
	@$(MAKE) configure

clean:
	@rm -f {/tmp,.}/{mirror,client,server,acds,discovery,ascii-chat}.{log,json} *.log *.out
	@if [ -d "$(BUILD_DIR)" ]; then \
		$(CMAKE) --build "$(BUILD_DIR)" --target clean; \
	else \
		echo "Nothing to clean (build directory '$(BUILD_DIR)' not found)"; \
	fi
	rm -rf web/*/dist
	rm -rf web/node_modules/.vite-temp
	rm -rf web/*/node_modules/.vite-temp

distclean:
	@echo "Removing build directory '$(BUILD_DIR)'..."
	@rm -rf "$(BUILD_DIR)"

checkprefix:
	@if [ -f "$(BUILD_DIR)/CMakeCache.txt" ] && [ -n "$(strip $(CMAKE_INSTALL_PREFIX))" ]; then \
		cached_prefix=$$($(CMAKE) -L -N "$(BUILD_DIR)" 2>/dev/null | grep 'CMAKE_INSTALL_PREFIX' | cut -d '=' -f2); \
		if [ "$(CMAKE_INSTALL_PREFIX)" != "$$cached_prefix" ]; then \
			printf "Re-running CMake: CMAKE_INSTALL_PREFIX '%s' does not match cached value '%s'.\n" "$(CMAKE_INSTALL_PREFIX)" "$$cached_prefix"; \
			rm -f "$(BUILD_DIR)/CMakeCache.txt"; \
		fi; \
	fi

install: checkprefix build
	@echo "Installing ascii-chat..."
	@env MAKEFLAGS= $(CMAKE) --install "$(BUILD_DIR)"

# =============================================================================
# File Discovery (for tooling targets)
# =============================================================================

C_FILES := $(wildcard $(SRC_DIR)/*.c) $(wildcard $(LIB_DIR)/*.c)
M_FILES := $(wildcard $(SRC_DIR)/*.m) $(wildcard $(LIB_DIR)/*.m)
C_HEADERS := $(wildcard $(SRC_DIR)/*.h) $(wildcard $(LIB_DIR)/*.h)


# =============================================================================
# Helper targets
# =============================================================================

.PHONY: format-docs format-config format-web format-other format-all help format format-check clang-tidy analyze cloc cloc-all-code

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
	@echo "  format-all      - Format tracked documentation, configuration, and web files"
	@echo "  format-docs     - Format documentation in docs/"
	@echo "  format-config   - Format TOML and Markdown outside docs/"
	@echo "  format-web      - Format all tracked web projects in parallel"
	@echo "  format-check    - Check code formatting without modifying files"
	@echo "  clang-tidy      - Run clang-tidy on sources"
	@echo "  analyze         - Run static analysis (clang --analyze, cppcheck)"
	@echo "  cloc            - Count lines of code for sections of the codebase like lib/ and src/ and web/"
	@echo "  cloc-all-code   - Count lines of code for the whole codebase"
	@echo "  help            - Show this help message"

format: format-all
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


CLOC_EXCLUDE := --exclude-dir=$(shell tr '\n' ',' < .clocignore)
CLOC_ARGS := --progress=1 --vcs=git $(CLOC_EXCLUDE)

cloc:
	@printf $(Purple)"documentation:\n"$(Reset)
	@cloc $(CLOC_ARGS) --force-lang='Markdown,dox' --force-lang='XML,in' "$(DOCS_DIR)"
	@printf $(Purple)"\n\nbuild configuration:\n"$(Reset)
	@cloc $(CLOC_ARGS) cmake/
	@printf $(Purple)"\n\ntests:\n"$(Reset)
	@cloc $(CLOC_ARGS) --include-lang='C,C/C++ Header,Bash,Bourne Shell,PowerShell,Dockerfile,YAML' tests/unit tests/performance tests/integration
	@printf $(Purple)"\n\nlibasciichat:\n"$(Reset)
	@cloc $(CLOC_ARGS) --include-lang='C,C/C++ Header,Objective-C' "$(LIB_DIR)"
	@printf $(Purple)"\n\nascii-chat executable:\n"$(Reset)
	@cloc $(CLOC_ARGS) --include-lang='C,C/C++ Header,Objective-C' "$(SRC_DIR)"
	@printf $(Purple)"\n\nwebsites:\n"$(Reset)
	@cloc $(CLOC_ARGS) ./web/*/src

cloc-all-code:
	@printf $(Purple)"All of ascii-chat repo's code (only code and real code, no TODOs or cmake config):\n"$(Reset)
	@cloc $(CLOC_ARGS) \
		tests/unit tests/performance tests/integration \
		"$(LIB_DIR)" \
 		"$(SRC_DIR)" \
 		./web/*/src

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

fresh-clone: build
	touch .env
	cp web/.env.example web/.env
	cd web && pnpm install && cd ..
FORMAT_PYTHON ?= uv run --project tests/ascii_chat_tests --frozen python

format-docs:
	@$(FORMAT_PYTHON) scripts/format-repo.py docs

format-config:
	@$(FORMAT_PYTHON) scripts/format-repo.py config

format-web:
	@$(FORMAT_PYTHON) scripts/format-repo.py web

format-other:
	@$(FORMAT_PYTHON) scripts/format-repo.py other

format-all:
	@$(FORMAT_PYTHON) scripts/format-repo.py all
