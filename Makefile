.DEFAULT_GOAL := dev

BUILD_TYPE ?= Dev
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# =============================================================================
# Build Mode Targets
# =============================================================================

# Production: musl + mimalloc + static (for releases)
production:
	@echo "========================================="
	@echo "Building ASCII-Chat - PRODUCTION"
	@echo "  - musl libc (static)"
	@echo "  - mimalloc allocator"
	@echo "  - Release optimization"
	@echo "========================================="
	cmake -B build-production -DCMAKE_BUILD_TYPE=Release -DUSE_MUSL=ON -DUSE_MIMALLOC=ON
	cmake --build build-production -j$(NPROC)
	@echo ""
	@ls -lh build-production/bin/ascii-chat-{server,client}

# Development: glibc + clang + DEBUG_MEMORY + sanitizers (for debugging)
development:
	@echo "========================================="
	@echo "Building ASCII-Chat - DEVELOPMENT"
	@echo "  - glibc (dynamic)"
	@echo "  - clang + sanitizers"
	@echo "  - DEBUG_MEMORY enabled"
	@echo "========================================="
	CC=clang CXX=clang++ cmake -B build-dev -DCMAKE_BUILD_TYPE=Debug -DUSE_MUSL=OFF -DUSE_MIMALLOC=OFF
	cmake --build build-dev -j$(NPROC)
	@echo ""
	@ls -lh build-dev/bin/ascii-chat-{server,client}

# Fast iteration: glibc + clang, no sanitizers (default)
dev: fast
fast:
	@echo "========================================="
	@echo "Building ASCII-Chat - FAST ITERATION"
	@echo "  - glibc (dynamic)"
	@echo "  - clang compiler"
	@echo "  - Debug symbols, no sanitizers"
	@echo "========================================="
	CC=clang CXX=clang++ cmake -B build-fast -DCMAKE_BUILD_TYPE=Dev -DUSE_MUSL=OFF -DUSE_MIMALLOC=OFF
	cmake --build build-fast -j$(NPROC)
	@echo ""
	@ls -lh build-fast/bin/ascii-chat-{server,client}

# Legacy targets (backwards compatibility)
./build:
	cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

./build/bin/ascii-chat-server: ./build
	cmake --build build --target ascii-chat-server

./build/bin/ascii-chat-client: ./build
	cmake --build build --target ascii-chat-client

all: ./build/bin/ascii-chat-server ./build/bin/ascii-chat-client

server: ./build/bin/ascii-chat-server

client: ./build/bin/ascii-chat-client

# =============================================================================
# Release Target
# =============================================================================

release:
	@if [ -z "$(VERSION)" ]; then \
		echo "Error: VERSION not specified. Usage: make release VERSION=v1.0.0"; \
		exit 1; \
	fi
	@./release.sh $(VERSION)

# =============================================================================
# Utility Targets
# =============================================================================

clean:
	rm -rf build/ build_*/ build-*/
	rm -f bin/*
	rm -f *.log
	rm -f compile_commands.json
	rm -rf release-*/
	rm -f *.tar.gz

cloc:
	@echo "LOC for ./src:"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='C,C/C++ Header,Objective-C' src | tail -n+2
	@echo && echo "LOC for ./lib:"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='C,C/C++ Header,Objective-C' lib | tail -n+2
	@echo && echo "LOC for ./tests:"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='C,C/C++ Header,Objective-C' tests | tail -n+2
	@echo && echo "LOC for ./ (conf):"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='YAML,JSON,INI,XML,Text' . | tail -n+2
	@echo && echo "LOC for ./ (scripting):"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='Powershell,Bourne Shell,Bourne Again Shell,CMake,make,Dockerfile' . | tail -n+2
	@echo && echo "LOC for ./ (docs):"
	@cloc --vcs=git --progress=1 --fmt=2 --include-lang='Markdown' . | tail -n+2

format:
	@if [ -n "$(FILE)" ]; then \
		echo "Formatting $(FILE) with .clang-format..."; \
		clang-format -style=file:.clang-format -i $(FILE); \
	else \
		echo "Formatting all source files (including tests) with .clang-format..."; \
		find src lib tests \( -name '*.c' -o -name '*.h' \) | xargs clang-format -style=file:.clang-format -i; \
	fi

format-check:
	@if [ -n "$(FILE)" ]; then \
		echo "Checking format of $(FILE) with .clang-format..."; \
		clang-format -style=file:.clang-format --dry-run --Werror $(FILE); \
	else \
		echo "Checking format of all source files (including tests) with .clang-format..."; \
		find src lib tests \( -name '*.c' -o -name '*.h' \) | xargs clang-format -style=file:.clang-format --dry-run --Werror; \
	fi

tidy:
	@if [ -n "$(FILE)" ]; then \
		echo "Running clang-tidy on $(FILE) with .clang-tidy..."; \
		clang-tidy --config-file=.clang-tidy $(FILE) -- @.clang -I./lib -I./src; \
	else \
		echo "Running clang-tidy on all lib/ and src/ files with .clang-tidy..."; \
		find src lib \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 -n1 -P8 sh -c 'clang-tidy --config-file=.clang-tidy "$$1" -- @.clang -I./lib -I./src' sh; \
	fi

scan-build:
	@echo "Running scan-build with CMake/Ninja..."
	@echo "Excluding system headers to avoid CET intrinsic false positives..."
	@echo "Cleaning build directory first..."
	rm -rf build/
	@echo "Configuring with scan-build..."
	scan-build --use-cc=/usr/bin/clang-20 --use-c++=/usr/bin/clang++ --status-bugs --exclude /usr --exclude /Applications/Xcode.app --exclude /Library/Developer cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DUSE_CCACHE=OFF -DUSE_PRECOMPILED_HEADERS=OFF -DCMAKE_C_FLAGS="-Wformat -Wformat-security -Werror=format-security"
	@echo "Building with scan-build..."
	scan-build --use-cc=/usr/bin/clang-20 --use-c++=/usr/bin/clang++ --status-bugs --exclude /usr --exclude /Applications/Xcode.app --exclude /Library/Developer cmake --build build --clean-first

.PHONY: production development dev fast release clean cloc format format-check tidy scan-build all server client
