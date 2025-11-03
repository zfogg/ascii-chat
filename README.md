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

ascii-chat relies on several key libraries for its functionality. Each dependency serves a specific purpose in the application:

### Core Dependencies

#### [BearSSL](https://bearssl.org/) - SSL/TLS Library
- **Purpose**: Building HTTPS client for fetching public keys from GitHub/GitLab
- **License**: MIT

#### [PortAudio](http://www.portaudio.com/) - Audio I/O Library
- **Purpose**: Cross-platform audio capture and playback for real-time audio streaming between clients
- **License**: MIT

#### [zstd](https://facebook.github.io/zstd/) - Compression Library
- **Purpose**: Compressing video frames before transmission (level 1 for optimal real-time performance)
- **License**: BSD/GPLv2

#### [libsodium](https://libsodium.org/) - Cryptographic Library
- **Purpose**: End-to-end encryption and authentication
- **License**: ISC

### Platform APIs (No External Dependencies)
**Update**: OpenCV is no longer required! The project now uses ✨ native platform APIs 🪄:
- **Linux**: V4L2 (Video4Linux2 kernel module)
- **macOS**: AVFoundation (macOS native API)
- **Windows**: Media Foundation (Windows native API)

### Linux
- **Ubuntu/Debian**: `apt-get install build-essential clang clang-tidy clang-format cmake ninja-build musl-tools musl-dev libmimalloc-dev libv4l-dev libzstd-dev portaudio19-dev libsodium-dev libcriterion-dev`
- **Arch**: `pacman -S pkg-config clang cmake ninja musl mimalloc v4l-utils zstd portaudio libsodium criterion`

### macOS
- `brew install cmake ninja zstd portaudio libsodium criterion`

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

   # Install required packages for a development build
   vcpkg install zstd:x64-windows portaudio:x64-windows libsodium:x64-windows mimalloc:x64-windows

   # If you want to do a release build:
   vcpkg install zstd:x64-windows-static portaudio:x64-windows-static libsodium:x64-windows-static
   ```

‼️ **Note:** Criterion, our test framework, is POSIX based, and so tests don't work on Windows natively. You can run tests via Docker with `./tests/scripts/run-docker-tests.ps1`.


## Build and run
1. Clone this repo onto a computer with a webcam and `cd` to its directory.
2. Install the dependencies for your OS (instructions listed above).
3. Run `cmake --preset default && cmake --build --preset default`.
4. Run `./build/bin/ascii-chat server`.
5. Open a second terminal window, tab, split, or pane. Or go to another computer.
6. Run `./build/bin/ascii-chat client`.
7. 👯 *Optional:* open more terminals and run more clients! ascii-chat is multiplayer 🔢. They'll all connect and show in a grid. On macOS you can just open multiple terminals and run `ascii-chat client` in each one. On Windows and Linux computers only one program can use a webcam at a time, so use multiple computers to test connecting multiple clients to the server (call a friend).

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
- `.\build.ps1` - A PowerShell script that kills running processes, cleans, configures, builds, and copies binaries to bin/. I tend to only use this to build when I develop on Windows.

### Configuration Options
CMake supports several configuration options:
- `-DCMAKE_C_COMPILER=clang` - Set compiler (default: auto-detected)
- `-DSIMD_MODE=auto` - SIMD mode: auto, sse2, ssse3, avx2, avx512, neon, sve (default: auto)
- `-DCRC32_HW=auto` - CRC32 hardware acceleration: auto, on, off (default: auto)
- `-DUSE_MUSL=ON` - Build a static binary with musl libc.
- `-DUSE_MIMALLOC=ON` - Build with Microsoft's mimalloc memory allocator (overrides malloc()/free() unless USE_MUSL is ON)
- Ninja automatically uses all available CPU cores for parallel builds


## Usage

ascii-chat uses a unified binary with two modes: `server` and `client`.

Start the server and wait for client connections:
```bash
bin/ascii-chat [--help|--version] [server|client] [options...]
# or on windows:
bin\ascii-chat.exe [--help|--version] [server|client] [options...]
```

Start the client and connect to a running server:
```bash
ascii-chat client [options]
```

For help with either mode:
```bash
ascii-chat server --help
ascii-chat client --help
```


## Command line flags

### Client Options

Run `./bin/ascii-chat client --help` to see all client options:

**Connection:**
- `-a --address ADDRESS`: IPv4 address to connect to (default: 127.0.0.1)
- `-H --host HOSTNAME`: Hostname for DNS lookup (alternative to --address)
- `-p --port PORT`: TCP port (default: 27224)

**Video:**
- `-x --width WIDTH`: Render width (auto-detected by default)
- `-y --height HEIGHT`: Render height (auto-detected by default)
- `-c --webcam-index INDEX`: Webcam device index (default: 0)
- `-f --webcam-flip`: Horizontally flip webcam (default: enabled)
- `--test-pattern`: Use test pattern instead of webcam (for debugging)
- `-s --stretch`: Stretch video to fit without preserving aspect ratio

**Display:**
- `--color-mode MODE`: Color modes: auto, mono, 16, 256, truecolor (default: auto)
- `--render-mode MODE`: Render modes: foreground, background, half-block (default: foreground)
- `-P --palette TYPE`: ASCII palette: standard, blocks, digital, minimal, cool, custom (default: standard)
- `-C --palette-chars CHARS`: Custom palette characters (implies --palette=custom)
- `--show-capabilities`: Display terminal color capabilities and exit
- `--utf8`: Force enable UTF-8/Unicode support
- `--fps FPS`: Desired frame rate 1-144 (default: 60)

**Audio:**
- `-A --audio`: Enable audio capture and playback

**Cryptography:**
- `-K --key FILE`: SSH/GPG key file for authentication: /path/to/key, gpg:keyid, github:user, gitlab:user, or 'ssh' for auto-detect
- `--password PASS`: Password for connection encryption
- `--no-encrypt`: Disable encryption (for local testing)
- `--server-key KEY`: Expected server public key for verification

**Misc:**
- `-q --quiet`: Disable console logging (logs only to file)
- `-S --snapshot`: Capture one frame and exit (useful for testing)
- `-D --snapshot-delay SECONDS`: Delay before snapshot in seconds (default: 3.0/5.0)
- `-L --log-file FILE`: Redirect logs to file
- `-v --version`: Display version information
- `-h --help`: Show help message

### Server Options

Run `./bin/ascii-chat server --help` to see all server options:

**Connection:**
- `-a --address ADDRESS`: IPv4 address to bind to (default: 0.0.0.0)
- `-p --port PORT`: TCP port to listen on (default: 27224)

**Display:**
- `-P --palette TYPE`: ASCII palette: standard, blocks, digital, minimal, cool, custom (default: standard)
- `-C --palette-chars CHARS`: Custom palette characters (implies --palette=custom)

**Audio:**
- Audio is always enabled on the server (no flag needed)

**Cryptography:**
- `-K --key FILE`: SSH key info for authentication: /path/to/key, github:user, gitlab:user, or 'ssh' for auto-detect
- `--password PASS`: Password for connection encryption
- `--no-encrypt`: Disable encryption (for local testing)
- `--client-keys FILE`: Allowed client keys file for authentication (whitelist)

**Misc:**
- `-L --log-file FILE`: Redirect logs to file
- `-v --version`: Display version information
- `-h --help`: Show help message



## Cryptography

ascii-chat supports **end-to-end encryption** using libsodium with Ed25519 key authentication and X25519 key exchange.

ascii-chat's crypto works like your web browser's HTTPS: the client and server perform the Diffie-Hellman exchange to establish secure communication with ephemeral keys every connection. HTTPS depends on certificates tied to DNS names with a certificate authority roots build into the operating system, but ascii-chat is built on TCP so DNS doesn't work for us to secure our servers. ascii-chat users need to verify their server's public keys manually until ACDS (ascii-chat discovery service) is built.

### Authentication Options

**SSH Key Authentication** (`--key`):
- Use your existing SSH Ed25519 keys for authentication
- Supports encrypted keys (prompts for passphrase or uses ssh-agent)
- Supports auto-detection with `--key ssh` or `--key ssh:`
- Supports GitHub public keys with `--key github:username`
- Future support planned for: `gpg:keyid`, `github:username.gpg`

**Password-Based Encryption** (`--password`):
- Simple password string for encrypting connections
- Can be combined with `--key` for dual authentication + encryption

**Ephemeral Keys** (default):
- When no authentication is provided, generates temporary keypair for the session

### Usage Examples

```bash
# SSH key authentication (prompts for passphrase if encrypted)
ascii-chat server --key ~/.ssh/id_ed25519
ascii-chat client --key ~/.ssh/id_ed25519

# Password-based encryption
ascii-chat server --password "my_secure_password"
ascii-chat client --password "my_secure_password"

# Both SSH key + password (double security)
ascii-chat server --key ~/.ssh/id_ed25519 --password "extra_encryption"
ascii-chat client --key ~/.ssh/id_ed25519 --password "extra_encryption"

# Auto-detect SSH key from ~/.ssh/
ascii-chat server --key ssh

# Disable encryption (for local testing)
ascii-chat server --no-encrypt
ascii-chat client --no-encrypt

# Server key verification (client verifies server identity)
ascii-chat client --key ~/.ssh/id_ed25519 --server-key ~/.ssh/server1.pub
# This .pub file format is standard OpenSSH public key format (ssh-ed25519).

# Client key whitelisting (server only accepts specific clients)
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys allowed_clients.txt
# This .txt file contains multiple .pub file contents, 1 per line, where each line is a client key that is allowed to connect to the server.

# Combine all three for maximum security!
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys ~/.ssh/client1.pub --password "password123"
ascii-chat client --key ~/.ssh/id_ed25519  --server-key ~/.ssh/server1.pub --password "password123"
# You need to know the server public key and the password before connecting, and the server needs to know your public key.
```


## Environment Variables

ascii-chat uses several environment variables for configuration and security
controls. These variables can be set to modify the program's behavior without
changing command-line arguments.

### Security Variables

#### `ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK`
- **Purpose**: Disables host identity verification (known_hosts checking)
- **Values**: `1` (enable), unset or any other value (disable, default)
- **⚠️ DANGER**: This completely bypasses security checks and makes connections vulnerable to man-in-the-middle attacks

#### `SSH_AUTH_SOCK`
- **Purpose**: SSH agent socket for secure key authentication
- **Values**: Path to SSH agent socket (e.g., `/tmp/ssh-XXXXXX/agent.12345`)
- **Security**: ✅ **Secure** - uses SSH agent for key management
- **When to use**: Preferred method for SSH key authentication (automatically detected)
- **Used for**: SSH key authentication without storing passphrases in environment

#### `ASCII_CHAT_SSH_PASSWORD`
- **Purpose**: Provides SSH key passphrase for encrypted SSH keys passed to --key
- **Values**: The passphrase string for your encrypted SSH key
- **Security**: ⚠️ **Sensitive data** - contains your SSH key passphrase - prefer ssh-agent over this (we support it)
- **When to use**: When using encrypted SSH keys and you want to avoid interactive passphrase prompts

### Terminal Variables (Used for Display Detection)

#### `TERM`
- **Purpose**: Terminal type detection for display capabilities
- **Usage**: Automatically set by terminal emulators
- **Used for**: Determining color support, character encoding, and display features

#### `COLORTERM`
- **Purpose**: Additional terminal color capability detection
- **Usage**: Automatically set by modern terminal emulators
- **Used for**: Enhanced color support detection beyond `TERM`

#### `LANG`, `LC_ALL`, `LC_CTYPE`
- **Purpose**: Locale and character encoding detection
- **Usage**: Automatically set by system locale
- **Used for**: UTF-8 support detection and character encoding

#### `TTY`
- **Purpose**: Terminal device detection
- **Usage**: Automatically set by terminal sessions
- **Used for**: Determining if running in a real terminal vs. script

#### `LINES`, `COLUMNS`
- **Purpose**: Terminal size detection for display dimensions
- **Usage**: Automatically set by terminal emulators
- **Used for**: Auto-detecting optimal video dimensions

### POSIX-Specific Variables

#### `USER`
- **Purpose**: Username detection for system identification on POSIX systems
- **Usage**: Automatically set by POSIX systems
- **Used for**: System user identification and logging

#### `HOME`
- **Purpose**: Determines user home directory for configuration files on POSIX systems
- **Usage**: Automatically detected by the system
- **Used for**:
  - SSH key auto-detection (`~/.ssh/`)
  - Configuration file paths (`~/.ascii-chat/`)
  - Path expansion with `~` prefix

### Windows-Specific Variables

#### `USERNAME`
- **Purpose**: Username detection for system identification on Windows
- **Usage**: Automatically set by Windows system
- **Used for**: System user identification and logging

#### `USERPROFILE`
- **Purpose**: Determines user home directory for configuration files on Windows
- **Usage**: Automatically detected by the Windows system
- **Used for**:
  - SSH key auto-detection (`~/.ssh/`)
  - Configuration file paths (`~/.ascii-chat/`)
  - Path expansion with `~` prefix

#### `_NT_SYMBOL_PATH`
- **Purpose**: Windows debug symbol path for crash analysis
- **Usage**: Automatically set by Windows debug tools
- **Used for**: Enhanced crash reporting and debugging

### Development/Testing Variables

#### `CI`
- **Purpose**: Continuous Integration environment detection
- **Values**: Any non-empty value indicates CI environment
- **Used for**: Adjusting test behavior and terminal detection in automated environments

#### `TESTING`, `CRITERION_TEST`
- **Purpose**: Test environment detection
- **Values**: Any non-empty value indicates test environment
- **Used for**: Reducing test data sizes and adjusting performance expectations


## Testing

The project uses a unified test runner script at `tests/scripts/run_tests.sh` that consolidates all test execution logic. It accepts all sorts of arguments and auto-builds the test executables it's gonna run beforehand with ninja, which is convenient because it allows you to simply iterate on code and then run this script, going between those two things.

### Quick Start
* Have the dependencies installed.
* Choose:
  1. Linux or macOS: run test runner script: `./tests/scripts/run_tests.sh`
  2. Windows: use Docker: `./tests/scripts/run-docker-tests.ps1` (just calls `run_tests.sh` in a container)

### Test Types
- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions and full workflows
- **Performance Tests**: Benchmark stuff like SIMD vs scalar implementations

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


## TODO
- [x] Audio.
- [x] Client should continuously attempt to reconnect
- [x] switch Client "-a/--address" option to "host" and make it accept domains as well as ipv4
- [x] Colorize ASCII output
- [ ] Refactor image processing algorithms
- [x] Grid packing algorithm.
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
- [x] Windows support.
- [x] Linux support.
- [x] Crypto.
- [ ] GPG key support for crypto (there's a bug upstream in libgcrypt).
- [ ] v4l2 webcam images working.


## Notes

- **Note:** Colored frames are many times larger than monochrome frames due
to the ANSI color codes.

- We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
