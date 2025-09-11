ascii-chat 📸
==========

ASCII video chat.

Probably the first command line video chat program.

It just prints ASCII, so it works on your rxvt-unicode in OpenBox, a Putty SSH
session, and even iTerm or Kitty.app on macOS.

It even works in an initial UNIX login shell, i.e. the login shell that runs
'startx'.

🆕 Now 3+ simultaneous people can connect and the server will render the clients to each other as a grid, like Google Hangouts and Zoom calls do.

🆕 Audio streaming is now supported via PortAudio, with a custom mixer with features like compression, ducking, crowd scaling, noise gating, hi/lo-pass filtering, and soft clipping.

![Animated demonstration: monochrome](http://i.imgur.com/E4OuqvX.gif)

![Animated demonstration: color](https://github.com/user-attachments/assets/3bbaaad0-2e62-46e8-9653-bd5201c4b7df)



## Dependencies
- Most people: `apt-get install build-essential clang pkg-config libv4l-dev zlib1g-dev portaudio19-dev libsodium-dev
libcriterion-dev`
- ArchLinux masterrace: `pacman -S clang pkg-config v4l-utils zlib portaudio libsodium libcriterion`
- macOS: `brew install pkg-config zlib portaudio libsodium criterion`

**Note:** OpenCV is no longer required! The project now uses native platform APIs:
- **Linux**: V4L2 (Video4Linux2)
- **macOS**: AVFoundation


## Build and run
- Clone this repo onto a computer with a webcam.
- Install the dependencies.
- Run `make`.
- Run `./bin/server -p 9001` in one terminal, and then
- Run `./bin/client -p 9001` in another.

Use `make -j debug` as you edit and test code (sometimes `make clean` too 😏).

Check the `Makefile` to see how it works.

## Available Make Targets

### Build Targets
- `make` or `make all` - Build all targets with default flags
- `make debug` - Build with debug symbols and no optimization
- `make debug-coverage` - Build with debug symbols and coverage
- `make release` - Build with optimizations enabled
- `make release-coverage` - Build with optimizations and coverage
- `make sanitize` - Build with address sanitizer for debugging
- `make clean` - Remove build artifacts

### Test Building Targets
- `make tests-debug` - Build test executables with debug flags
- `make tests-release` - Build test executables with release flags
- `make tests-debug-coverage` - Build test executables with debug + coverage
- `make tests-release-coverage` - Build test executables with release + coverage

### Test Running Targets
- `make test` - Run all tests in debug mode
- `make test-release` - Run all tests in release mode

### Development Tools
- `make format` - Format source code using clang-format
- `make format-check` - Check code formatting without modifying files
- `make clang-tidy` - Run clang-tidy on sources
- `make analyze` - Run static analysis (clang --analyze, cppcheck)
- `make scan-build` - Run scan-build static analyzer
- `make cloc` - Count lines of code
- `make compile_commands.json` - Generate compile_commands.json for IDE support

## Utility Targets
- `make help` - Show all available targets and configuration
- `make install-hooks` - Install git hooks from git-hooks/ directory
- `make uninstall-hooks` - Remove installed git hooks
- `make todo` - Build the ./todo subproject
- `make todo-clean` - Clean the ./todo subproject

### Configuration
The Makefile supports several configuration options:
- `CC=clang` - Set compiler (default: clang)
- `CSTD=c23` - Set C standard (default: c23)
- `SIMD_MODE=auto` - SIMD mode: auto, sse2, ssse3, avx2, avx512, neon, sve (default: auto)
- `CRC32_HW=auto` - CRC32 hardware acceleration: auto, on, off (default: auto)
- `MAKEFLAGS=-j$(nproc)` - Parallel build jobs (Linux) or `-j$(sysctl -n hw.logicalcpu)` (macOS)

## Testing

The project uses a unified test runner script (`tests/scripts/run_tests.sh`) that consolidates all test execution logic.

### Quick Start
1. Have the dependencies installed.
2. Run `make test` (debug mode) or `make test-release` (release mode).

### Test Types
- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions and full workflows
- **Performance Tests**: Benchmark SIMD vs scalar implementations

### Using the Test Script Directly
```bash
# Run all tests in debug mode
./tests/scripts/run_tests.sh

# Run specific test types
./tests/scripts/run_tests.sh -t unit
./tests/scripts/run_tests.sh -t integration
./tests/scripts/run_tests.sh -t performance

# Run with different build configurations
./tests/scripts/run_tests.sh -b debug
./tests/scripts/run_tests.sh -b release
./tests/scripts/run_tests.sh -b debug-coverage
./tests/scripts/run_tests.sh -b release-coverage
./tests/scripts/run_tests.sh -b sanitize

# Generate JUnit XML for CI
./tests/scripts/run_tests.sh -J

# Run in parallel (default: number of CPU cores)
./tests/scripts/run_tests.sh -j 4

# Verbose output
./tests/scripts/run_tests.sh -v
```

### Manual Test Execution
You can also run individual test executables directly:
```bash
# Build test executables first
make tests-debug

# Run individual tests
bin/test_mixer --verbose
bin/test_ascii_simd_performance --filter "monochrome"
```

### Testing Framework
- **Framework**: [libcriterion](https://criterion.readthedocs.io/en/master/)
- **Coverage**: Code coverage reports generated in CI
- **Performance**: SIMD performance tests with aggressive speedup expectations (1-4x)
- **Memory Checking**: AddressSanitizer support via `-b sanitize` for detecting memory issues


## Cryptography
🔴⚠️ NOT YET IMPLEMENTED 🔴⚠️

Good news though: we have **libsodium** installed and some code written for it.

🔜 TODO: Implement crypto.


## Command line flags

### Client Options

Run `./bin/client -h` to see all client options:

- `-a --address ADDRESS`: IPv4 address to connect to (default: 0.0.0.0)
- `-p --port PORT`: TCP port (default: 27224)
- `-x --width WIDTH`: Render width (auto-detected by default)
- `-y --height HEIGHT`: Render height (auto-detected by default)
- `-c --webcam-index INDEX`: Webcam device index (default: 0)
- `-f --webcam-flip`: Horizontally flip webcam (default: enabled)
- `--color-mode MODE`: Color modes: auto, mono, 16, 256, truecolor (default: auto)
- `--show-capabilities`: Display terminal color capabilities and exit
- `--utf8`: Force enable UTF-8/Unicode support
- `-M --background-mode MODE`: Render colors for glyphs or cells: foreground, background (default: foreground)
- `-A --audio`: Enable audio capture and playback
- `-s --stretch`: Stretch video to fit without preserving aspect ratio
- `-q --quiet`: Disable console logging (logs only to file)
- `-S --snapshot`: Capture one frame and exit (useful for testing)
- `-D --snapshot-delay SECONDS`: Delay before snapshot in seconds (default: 3.0/5.0)
- `-L --log-file FILE`: Redirect logs to file
- `-E --encrypt`: Enable AES encryption
- `-K --key PASSWORD`: Encryption password
- `-F --keyfile FILE`: Read encryption key from file
- `-h --help`: Show help message

### Server Options

Run `./bin/server -h` to see all server options:

- `-a --address ADDRESS`: IPv4 address to bind to (default: 0.0.0.0)
- `-p --port PORT`: TCP port to listen on (default: 27224)
- `-A --audio`: Enable audio mixing and streaming
- `-L --log-file FILE`: Redirect logs to file
- `-E --encrypt`: Enable AES encryption
- `-K --key PASSWORD`: Encryption password
- `-F --keyfile FILE`: Read encryption key from file
- `-h --help`: Show help message


## Usage

Start the server and wait for client connections:
```bash
./bin/server [options]
```

Start the client and connect to a running server:
```bash
./bin/client [options]
```

## TODO
- [x] Audio.
- [x] Client should continuously attempt to reconnect
- [ ] switch Client "-a/--address" option to "host" and make it accept domains as well as ipv4
- [x] Colorize ASCII output
- [ ] Refactor image processing algorithms
- [x] client reconnect logic
- [x] terminal resize events
- [x] A nice protocol for the thing (packets and headers).
- [x] client requests a frame size
- [x] Client should gracefully handle `frame width > term width`
- [x] Client should gracefully handle `term resize` event
- [ ] Compile to WASM/WASI and run in the browser
- [x] Socket multiplexing.
- [ ] Edge detection and other things like that to make the image nicer.
- [x] Multiple clients. Grid to display them.
- [x] Snapshot mode for clients with --snapshot to "take a photo" of a call and print it to the terminal or a file, rather than rendering video for a long time.
- [x] Audio mixing for multiple clients with compression and ducking.
- [ ] Color filters so you can pick a color for all the ascii so it can look like the matrix when you pick green (Gurpreet suggested).
- [ ] Lock-free packet send queues.
- [x] Hardware-accelerated ASCII-conversion via SIMD.


## Notes

- **Note:** Colored frames are many times larger than monochrome frames due
to the ANSI color codes.

- We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
