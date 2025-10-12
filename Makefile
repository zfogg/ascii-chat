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
	@ls -lh build-production/bin/ascii-chat

# Profile-Guided Optimization: 3-stage build for maximum performance
# Note: PGO requires glibc, not musl (musl lacks __*_chk fortified functions needed by libgcov)
# This is NOT used for official releases - use 'make production' for musl static builds
pgo:
	@echo "========================================="
	@echo "Building ASCII-Chat - PGO (3 stages)"
	@echo "  WARNING: PGO uses glibc (dynamic)"
	@echo "  For official releases, use 'make production'"
	@echo "========================================="
	@echo "  Stage 1: Instrumented build"
	@echo "  Stage 2: Profile collection"
	@echo "  Stage 3: Optimized build"
	@echo "========================================="
	@echo ""
	@echo "Stage 1/3: Building instrumented binaries (glibc + dynamic)..."
	@rm -rf build-pgo-profile build-pgo
	cmake -B build-pgo-profile -DCMAKE_BUILD_TYPE=Release \
		-DUSE_MUSL=OFF -DUSE_MIMALLOC=ON \
		-DCMAKE_C_FLAGS="-fprofile-generate=/root/src/github.com/zfogg/ascii-chat/pgo-data" \
		-DCMAKE_EXE_LINKER_FLAGS="-fprofile-generate=/root/src/github.com/zfogg/ascii-chat/pgo-data"
	cmake --build build-pgo-profile -j$(NPROC)
	@echo ""
	@echo "Stage 2/3: Collecting profile data..."
	@echo "  Running server with 4-client workload (1 webcam + 3 test patterns)..."
	@echo "  (Will run for 35 seconds to collect comprehensive profiles)"
	@mkdir -p pgo-data
	@rm -f pgo-data/*.gcda 2>/dev/null || true
	@(build-pgo-profile/bin/ascii-chat server --port 27777 > /dev/null 2>&1 &); \
	SERVER_PID=$$!; \
	sleep 2; \
	timeout 35 build-pgo-profile/bin/ascii-chat client --address 127.0.0.1 --port 27777 --snapshot --snapshot-delay 30 > /dev/null 2>&1 & \
	CLIENT1_PID=$$!; \
	sleep 1; \
	timeout 35 build-pgo-profile/bin/ascii-chat client --address 127.0.0.1 --port 27777 --test-pattern --snapshot --snapshot-delay 30 > /dev/null 2>&1 & \
	CLIENT2_PID=$$!; \
	sleep 1; \
	timeout 35 build-pgo-profile/bin/ascii-chat client --address 127.0.0.1 --port 27777 --test-pattern --snapshot --snapshot-delay 30 > /dev/null 2>&1 & \
	CLIENT3_PID=$$!; \
	sleep 1; \
	timeout 35 build-pgo-profile/bin/ascii-chat client --address 127.0.0.1 --port 27777 --test-pattern --snapshot --snapshot-delay 30 > /dev/null 2>&1 & \
	CLIENT4_PID=$$!; \
	wait $$CLIENT1_PID $$CLIENT2_PID $$CLIENT3_PID $$CLIENT4_PID 2>/dev/null || true; \
	sleep 1; \
	kill $$SERVER_PID 2>/dev/null || true; \
	wait $$SERVER_PID 2>/dev/null || true
	@echo "  Profile data collected: $$(ls pgo-data/*.gcda 2>/dev/null | wc -l) files"
	@echo ""
	@echo "Stage 3/3: Building optimized binaries with profile data (glibc + dynamic)..."
	cmake -B build-pgo -DCMAKE_BUILD_TYPE=Release \
		-DUSE_MUSL=OFF -DUSE_MIMALLOC=ON \
		-DCMAKE_C_FLAGS="-fprofile-use=/root/src/github.com/zfogg/ascii-chat/pgo-data -fprofile-correction" \
		-DCMAKE_EXE_LINKER_FLAGS="-fprofile-use=/root/src/github.com/zfogg/ascii-chat/pgo-data"
	cmake --build build-pgo -j$(NPROC)
	@echo ""
	@echo "========================================="
	@echo "PGO Build Complete (glibc + dynamic)!"
	@echo "========================================="
	@echo "PGO-optimized build (glibc, NOT for release):"
	@ls -lh build-pgo/bin/ascii-chat
	@echo ""
	@echo "NOTE: For official releases, use 'make production' (musl + static)"
	@echo "Profile data: pgo-data/ ($$(du -sh pgo-data 2>/dev/null | cut -f1))"

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
	@ls -lh build-dev/bin/ascii-chat

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
	@ls -lh build-fast/bin/ascii-chat

# Legacy targets (backwards compatibility with unified binary)
./build:
	cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

./build/bin/ascii-chat: ./build
	cmake --build build --target ascii-chat

all: ./build/bin/ascii-chat


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
