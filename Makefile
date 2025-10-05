.DEFAULT_GOAL := all

BUILD_TYPE ?= Debug

./build:
	cmake -B build -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

./build/bin/ascii-chat-server: ./build
	cmake --build build --target ascii-chat-server

./build/bin/ascii-chat-client: ./build
	cmake --build build --target ascii-chat-client

all: ./build/bin/ascii-chat-server ./build/bin/ascii-chat-client

server: ./build/bin/ascii-chat-server

client: ./build/bin/ascii-chat-client

clean:
	rm -rf build/ build_*/
	rm -f bin/*
	rm -f *.log
	rm -f compile_commands.json

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
		echo "Formatting $(FILE)..."; \
		clang-format -i $(FILE); \
	else \
		echo "Formatting all source files..."; \
		find src lib -name '*.c' -o -name '*.h' | xargs clang-format -i; \
	fi

format-check:
	@if [ -n "$(FILE)" ]; then \
		echo "Checking format of $(FILE)..."; \
		clang-format --dry-run --Werror $(FILE); \
	else \
		echo "Checking format of all source files..."; \
		find src lib -name '*.c' -o -name '*.h' | xargs clang-format --dry-run --Werror; \
	fi

tidy:
	@if [ -n "$(FILE)" ]; then \
		echo "Running clang-tidy on $(FILE)..."; \
		clang-tidy $(FILE) -- -I./lib -I./src; \
	else \
		echo "Running clang-tidy on all source files..."; \
		find src lib -name '*.c' | xargs -n1 -P8 clang-tidy -- -I./lib -I./src; \
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

.PHONY: clean cloc format format-check tidy scan-build
