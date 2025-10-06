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

![Animated demonstration: monochrome](https://i.imgur.com/E4OuqvX.gif)

![Animated demonstration: color](https://github.com/user-attachments/assets/3bbaaad0-2e62-46e8-9653-bd5201c4b7df)



## Dependencies

**Update**: OpenCV is no longer required! The project now uses ✨ native platform APIs 🪄:
- **Linux**: V4L2 (Video4Linux2)
- **macOS**: AVFoundation
- **Windows**: Media Foundation

### Linux
- **Ubuntu/Debian**: `apt-get install build-essential clang cmake ninja-build musl-tools musl-dev libmimalloc-dev libv4l-dev zlib1g-dev portaudio19-dev libsodium-dev libcriterion-dev`
- **Arch**: `pacman -S pkg-config clang cmake ninja musl mimalloc v4l-utils zlib portaudio libsodium criterion`

### macOS
- `brew install cmake ninja zlib portaudio libsodium criterion`

### Windows
1. **Install Scoop** (if not already installed):
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   irm get.scoop.sh | iex
   ```

2. **Install build tools via Scoop**:
   ```powershell
   scoop install cmake ninja llvm
   ```

3. **Install Windows SDK**:
   - Download and install [Visual Studio Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)
   - Or install via Scoop: `scoop install windows-sdk-10-version-2004`

4. **Install dependencies via vcpkg**:
   ```powershell
   # Install vcpkg (if not already installed)
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat

   # Install required packages
   .\vcpkg install zlib:x64-windows portaudio:x64-windows libsodium:x64-windows
   ```

‼️ **Note:** Criterion, our test framework, is POSIX based, and so tests don't work on Windows natively. You can run tests via Docker with `./tests/scripts/run-docker-tests.ps1`.


## Build and run
1. Clone this repo onto a computer with a webcam and `cd` to its directory.
2. Install the dependencies for your OS (instructions listed above).
3. Run `cmake --preset default && cmake --build --preset default`.
4. Run `./build/bin/ascii-chat-server`.
5. Open a second terminal window/tab/split/pane.
6. Run `./build/bin/ascii-chat-client`.
7. *Optional:* open more terminals and run more clients! ascii-chat is multiplayer 🔢. They'll all connect and show in a grid. On macOS you can just open multiple terminals and run `ascii-chat-client` in each one. On Windows and Linux computers only one program can use a webcam at a time, so use multiple computers to test connecting multiple clients to the server (call a friend).

Check the `CMakeLists.txt` to see how it works.

## Available CMake Presets

### Development Builds
- **`default`** / **`debug`** - Debug build with AddressSanitizer (slowest, catches most bugs)
- **`dev`** - Debug symbols without sanitizers (faster iteration)
- **`coverage`** - Build with coverage instrumentation

### Production Builds
- **`release`** - Optimized static release build with musl + mimalloc (Linux deployment)
  - Produces stripped static binaries (~700KB) using musl libc
  - Best for Linux production deployment - single binary, no dependencies
  - Uses mimalloc for optimal memory performance

- **`release-clang`** - Optimized dynamic release build with clang + mimalloc (Windows/macOS)
  - Produces stripped dynamic binaries (~200KB) using system libc
  - Best for Windows/macOS where musl isn't available
  - Uses mimalloc for optimal memory performance

- **`release-musl`** - Alias for `release` (static musl build)

### Profiling/Production Debugging
- **`relwithdebinfo`** - Optimized build with debug symbols (for profiling and debugging production issues)
  - Optimized with `-O2` but keeps debug symbols (not stripped, ~1.3MB)
  - Use with `gdb`, `lldb`, `perf`, `valgrind` for profiling
  - Uses clang + glibc for best debugging experience
  - Includes mimalloc for realistic performance profiling

### Building
```bash
# Development (default)
cmake --preset default && cmake --build build

# Production release (Linux static binary)
cmake --preset release && cmake --build build

# Production release (Windows/macOS dynamic binary)
cmake --preset release-clang && cmake --build build

# Profiling/debugging production issues
cmake --preset relwithdebinfo && cmake --build build

# Clean rebuild
rm -rf build
cmake --preset release && cmake --build build
```

### What is musl and mimalloc?

**musl libc**: A lightweight, fast, and simple C standard library alternative to glibc. The `release` preset uses musl to create **statically linked binaries** that have no external dependencies - perfect for deployment as they work on any Linux system without requiring specific libraries to be installed.

**mimalloc**: Microsoft's high-performance memory allocator. All release and profiling builds use mimalloc instead of the system allocator for better performance. It provides:
- Up to 2x faster allocation/deallocation
- Better memory locality and cache performance
- Lower memory fragmentation
- Optimized for multi-threaded workloads

### Development Tools
- `cmake --build --preset debug --target format` - Format source code using clang-format
- `cmake --build --preset debug --target format-check` - Check code formatting
- `cmake --build --preset debug --target clang-tidy` - Run clang-tidy on sources

### Configuration Options
CMake supports several configuration options:
- `-DCMAKE_C_COMPILER=clang` - Set compiler (default: auto-detected)
- `-DSIMD_MODE=auto` - SIMD mode: auto, sse2, ssse3, avx2, avx512, neon, sve (default: auto)
- `-DCRC32_HW=auto` - CRC32 hardware acceleration: auto, on, off (default: auto)
- Ninja automatically uses all available CPU cores for parallel builds

## Testing

The project uses a unified test runner script (`tests/scripts/run_tests.sh`) that consolidates all test execution logic.

### Quick Start
1. Have the dependencies installed.
2. Build the project: `cmake --preset debug && cmake --build --preset debug`
3. Run tests: `./tests/scripts/run_tests.sh`

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

# Generate JUnit XML for CI
./tests/scripts/run_tests.sh -J

# Run in parallel (default: number of CPU cores)
./tests/scripts/run_tests.sh -j 4

# Verbose output
./tests/scripts/run_tests.sh -v
```

### Windows Docker Testing
On Windows, since Criterion is POSIX-based, tests must be run in a Docker container. Use the PowerShell wrapper script:

```powershell
# Run all tests
./tests/scripts/run-docker-tests.ps1

# Run specific test types
./tests/scripts/run-docker-tests.ps1 unit
./tests/scripts/run-docker-tests.ps1 integration
./tests/scripts/run-docker-tests.ps1 performance

# Run specific tests
./tests/scripts/run-docker-tests.ps1 unit options
./tests/scripts/run-docker-tests.ps1 unit buffer_pool packet_queue

# Run with verbose output
./tests/scripts/run-docker-tests.ps1 unit options -VerboseOutput

# Run with different build types
./tests/scripts/run-docker-tests.ps1 unit -BuildType release

# Run clang-tidy static analysis
./tests/scripts/run-docker-tests.ps1 clang-tidy
./tests/scripts/run-docker-tests.ps1 clang-tidy lib/common.c

# Interactive shell for debugging
./tests/scripts/run-docker-tests.ps1 -Interactive
```

The Docker script automatically:
- Builds the test container if needed
- Mounts your source code for live testing
- Handles incremental builds
- Provides the same test interface as the native script

### Manual Test Execution
You can also run individual test executables directly:
```bash
# Build the project first
cmake --preset debug && cmake --build --preset debug

# Run individual tests
build/bin/test_unit_mixer --verbose
build/bin/test_performance_ascii_simd --filter "*monochrome*"
```

### Testing Framework
- **Framework**: [libcriterion](https://criterion.readthedocs.io/en/master/)
- **Coverage**: Code coverage reports generated in CI
- **Performance**: SIMD performance tests with aggressive speedup expectations (1-4x)
- **Memory Checking**: Comprehensive sanitizer support via `-b debug` for detecting memory issues, undefined behavior, and more


## Cryptography
🔴⚠️ NOT YET IMPLEMENTED 🔴⚠️

Good news though: we have **libsodium** installed and some code written for it.

🔜 TODO: Implement crypto.


## Command line flags

### Client Options

Run `./bin/ascii-chat-client -h` to see all client options:

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

Run `./bin/ascii-chat-server -h` to see all server options:

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
./bin/ascii-chat-server [options]
```

Start the client and connect to a running server:
```bash
./bin/ascii-chat-client [options]
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
