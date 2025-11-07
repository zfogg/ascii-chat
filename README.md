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



## Table of Contents

- [Get ascii-chat](#get-ascii-chat)
- [Usage](#usage)
- [Command line flags](#command-line-flags)
- [Cryptography](#cryptography)
- [Environment Variables](#environment-variables)
- [Open Source](#open-source)
  - [Dependencies](#dependencies)
    - [Core Dependencies](#core-dependencies)
    - [Operating System APIs](#operating-system-apis)
    - [Install Dependencies on Linux or macOS](#install-dependencies-on-linux-or-macos)
    - [Install Dependencies on Windows](#install-dependencies-on-windows)
  - [Build from source](#build-from-source)
    - [What is musl and mimalloc?](#what-is-musl-and-mimalloc)
    - [Development Tools](#development-tools)
    - [Configuration Options](#configuration-options)
    - [Documentation](#documentation)
  - [Testing](#testing)
    - [Quick Start](#quick-start)
    - [Test Types](#test-types)
    - [Using the Test Script Directly](#using-the-test-script-directly)
    - [Windows Docker Testing](#windows-docker-testing)
    - [Manual Test Execution](#manual-test-execution)
    - [Testing Framework](#testing-framework)
- [TODO](#todo)
- [Notes](#notes)


## Get ascii-chat

- Latest release: [v0.3.5](https://github.com/zfogg/ascii-chat/releases/tag/v0.3.5)
- All downloads: [GitHub Releases](https://github.com/zfogg/ascii-chat/releases)
- Source installation: see the [Dependencies](#dependencies) section below, then follow the [Build from source](#build-from-source) steps


## Usage

ascii-chat uses a unified binary with two modes: `server` and `client`.

Start the server and wait for client connections:
```bash
# NOTE: on Windows the filename is ascii-chat.exe
ascii-chat [--help|--version] [server|client] [options...]
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

Run `ascii-chat client --help` to see all client options:

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
ascii-chat server --password "hunter2"
ascii-chat client --password "hunter2"

# Both SSH key + password (double security)
ascii-chat server --key ~/.ssh/id_ed25519 --password "extra_encryption"
ascii-chat client --key ~/.ssh/id_ed25519 --password "extra_encryption"

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
# You need to know (1) the server public key and (2) the password before connecting, and the server needs to know (3) your public key and (4) the same password.
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


## Open Source

ascii-chat is an open source project with an MIT license, meaning you can and should use our code to make cool stuff. Follow through this Open Source section of the README and you'll be compiling against libasciichat in no time.

The first thing you need to do is get the dependencies. Some of them are git submodules of the repo. When you clone the repo get the submodules too.

```bash
git submodule init
git submodule update --recursive
```

Now let's list out and talk about the dependencies before we install them.

### Dependencies

ascii-chat relies on several libraries.

#### Core Dependencies

##### [PortAudio](http://www.portaudio.com/) - Audio I/O Library
- **Purpose**: So the clients can talk to and hear each other. This library provides audio via the same interface on all three major operating systems, which is really neat because for webcam code I have to work with three different operating system APIs to build ascii-chat.
- **License**: MIT

##### [tomlc17](https://github.com/cktan/tomlc17) - TOML File Library
- **Purpose**: Modern implementation of TOML for config file parsing and editing. For `~/.config/ascii-chat/config.toml`.
- **License**: MIT

##### [uthash](https://troydhanson.github.io/uthash/) - Hash Table Library
- **Purpose**: Gives us fast O(1) lookups for the server's client manager, and persistent memory for caching data
- **License**: BSD revised

##### [libsodium](https://libsodium.org/) - Cryptographic Library
- **Purpose**: I use this for the crypto protocol, which you can read more about below. End-to-end encryption and authentication of the protocol's packets, with features like password protection and re-keying.
- **License**: ISC

##### [libsodium-bcrypt-pbkdf](https://github.com/imaami/libsodium-bcrypt-pbkdf) - libsodium-Compatible Code
- **Purpose**: Exports a single function that does the blowfish cipher key derivation needed for decrypting ed25519 keys. This library for bcrypt + BearSSL for aes-ctr and aes-cbc + libsodium crypto algorithms = the ability to decrypt and use password-protected ~/.ssh/id_ed25519 files.
- **License**: [none]

##### [BearSSL](https://bearssl.org/) - SSL/TLS Library
- **Purpose**: We need this for our custom HTTPS client, to fetch public keys from GitHub/GitLab for encryption authorization. We also use its aes-ctr and aes-cbc functions with libsodium-bcrypt-pkdf to decrypt ed25519 keys.
- **License**: MIT

##### [zstd](https://facebook.github.io/zstd/) - Compression Library
- **Purpose**: To make the protocol more efficient. Makes all the protocol packets smaller at the cost of some compute time every frame. Check out `lib/compression.c`, it's pretty small.
- **License**: BSD/GPLv2

##### [Sokol](https://github.com/floooh/sokol) - Utility Library
- **Purpose**: Header-only C library providing simple cross-platform APIs (random collection of header-only SDKs for things like timing and audio and async downloads).
- **License**: zlib/libpng

#### Operating System APIs
ascii-chat uses native platform APIs for each platform for webcam access:
- **Linux**: V4L2 Linux kernel module
- **macOS**: AVFoundation native macOS API
- **Windows**: Media Foundation native Windows API

#### Install Dependencies on Linux or macOS
- **Plain old Ubuntu**: `apt-get install clang clang-tidy clang-format cmake ninja-build musl-tools musl-dev libmimalloc-dev libzstd-dev portaudio19-dev libsodium-dev libcriterion-dev`
- **Glorious Arch Linux**: `pacman -S pkg-config clang cmake ninja musl mimalloc zstd portaudio libsodium criterion`
- **macOS**: - `brew install cmake ninja zstd portaudio libsodium criterion`

#### Install Dependencies on Windows
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
   vcpkg install zstd:x64-windows portaudio:x64-windows libsodium:x64-windows

   # If you want to do a release build
   vcpkg install zstd:x64-windows-static portaudio:x64-windows-static libsodium:x64-windows-static mimalloc:x64-windows
   ```

‼️ **Note:** Criterion, our test framework, is POSIX based, and so tests don't work on Windows natively. You can run tests via Docker with `./tests/scripts/run-docker-tests.ps1`.

### Build from source

For open source developers who want a working copy:

1. Clone this repository: `git clone git@github.com:zfogg/ascii-chat.git; ls ascii-chat && cd ascii-chat`.
2. Install the dependencies for your platform (see [Dependencies](#dependencies) above or the docs).
3. Build an optimized-with-debug build: `make CMAKE_BUILD_TYPE=RelWithDebInfo`.
4. Run `./build/bin/ascii-chat server`.
5. Open a second terminal window, tab, split, or pane. Or go to another computer.
6. Run `./build/bin/ascii-chat client`.
7. 👯 *Optional:* open more terminals and run more clients! ascii-chat is multiplayer 🔢. They'll all connect and show in a grid. On macOS you can just open multiple terminals and run `ascii-chat client` in each one. On Windows and Linux computers only one program can use a webcam at a time, so use multiple computers to test connecting multiple clients to the server (call a friend).

`make` configures CMake for you; if you prefer manual steps, you can still run cmake directly: `cmake --preset default && cmake --build --preset default` (or choose another --preset).

#### What is musl and mimalloc?

**musl libc**: A lightweight, fast, and simple C standard library alternative to glibc. The `release` preset uses musl to create **statically linked binaries** that have no external dependencies - perfect for deployment as they work on any Linux system without requiring specific libraries to be installed.

**mimalloc**: Microsoft's high-performance memory allocator. All release and profiling builds use mimalloc instead of the system allocator for better performance. It provides:
- Up to 2x faster allocation/deallocation
- Better memory locality and cache performance
- Lower memory fragmentation
- Optimized for multi-threaded workloads

#### Development Tools
- `cmake --build --preset dev --target ascii-chat` - Build `ascii-chat` without sanitizers
- `cmake --build --preset debug --target format` - Format all source code using clang-format
- `cmake --build --preset debug --target clang-tidy` - Run clang-tidy across the tree
- `cmake --build --preset debug --target docs` - Generate the Doxygen API reference into `build/docs/html`
- `cmake --build --preset debug --target docs-open` - Build and open the docs in your default browser
- `cmake --build --preset release --target package-productbuild` - Build a release and make a `.pkg` installer for macOS (analogous targets exist per platform)
- `./build.ps1` - PowerShell helper that stops running binaries, cleans, configures, builds, and syncs artifacts into `bin/` when developing on Windows
- `./scripts/*` - Developer utilities (dependency installers, helper scripts, etc.)

#### Configuration Options
Pass these Boolean options via `-D<option>=ON|OFF` when configuring CMake (for example `cmake -B build -DASCIICHAT_ENABLE_ANALYZERS=ON`). Defaults reflect the logic in our CMake modules.

- `USE_MUSL` (Linux only): defaults to `ON` for `Release`/`RelWithDebInfo` builds to produce static PIE binaries with musl + mimalloc; `OFF` for other build types and on non-Linux hosts.
- `USE_MIMALLOC`: defaults to `ON` whenever we're optimizing for performance (Release/RelWithDebInfo or musl builds), and `OFF` for `Debug`/`Dev` configurations to keep debugging predictable.
- `BUILD_TESTS`: defaults to `ON` so Criterion unit/integration/performance tests are compiled.
- `USE_PRECOMPILED_HEADERS`: defaults to `ON` (requires CMake ≥ 3.16 and is disabled automatically for musl builds) to accelerate core library builds.
- `USE_CCACHE`: defaults to `ON` to speed up rebuilds with `ccache` (forced `OFF` when musl is enabled).
- `USE_CPACK`: defaults to `ON` to make packaging targets (`package`, `package-productbuild`, etc.) available.
- `ASCIICHAT_RELEASE_ENABLE_FAST_MATH`: defaults to `OFF`; flip to `ON` to allow aggressive fast-math optimizations in Release builds.
- `ASCIICHAT_RELEASE_KEEP_FRAME_POINTERS`: defaults to `ON`; set `OFF` if you want slightly tighter Release binaries and are okay with poorer stack traces.
- `ASCIICHAT_ENABLE_ANALYZERS`: defaults to `OFF`; enable to wire clang-tidy/cppcheck into the build (respecting `ASCIICHAT_CLANG_TIDY`/`ASCIICHAT_CPPCHECK` overrides).
- `ASCIICHAT_ENABLE_UNITY_BUILDS`: defaults to `OFF`; enable to batch-compile sources for faster rebuilds on some toolchains.
- `ASCIICHAT_ENABLE_CTEST_DASHBOARD`: defaults to `OFF`; enable to include CTest dashboard configuration (`include(CTest)`).

#### Documentation
- Install Doxygen (`brew install doxygen`, `apt-get install doxygen`, etc.) to enable the documentation targets.
- Run `cmake --build build --target docs` to generate HTML + manpage docs in `build/docs/`.
- Run `cmake --build build --target docs-open` to generate the docs and open `build/docs/html/index.html` in your default browser (works on macOS, Linux, and Windows).

### Testing

#### Testing Framework
- **Framework**: [libcriterion](https://criterion.readthedocs.io/en/master/)
- **Coverage**: Code coverage reports generated in CI
- **Performance**: SIMD performance tests with aggressive speedup expectations (1-4x)
- **Memory Checking**: Comprehensive sanitizer support via `-b debug` for detecting memory issues, undefined behavior, and more

The project uses a unified test runner script at `tests/scripts/run_tests.sh` that consolidates all test execution logic. It accepts all sorts of arguments and auto-builds the test executables it's gonna run beforehand with ninja, which is convenient because it allows you to simply iterate on code and then run this script, going between those two things.

#### Quick Start
* Have the dependencies installed.
* Choose:
  1. Linux or macOS: run test runner script: `./tests/scripts/run_tests.sh`
  2. Windows: use Docker: `./tests/scripts/run-docker-tests.ps1` (just calls `run_tests.sh` in a container)

#### Test Types
- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions and full workflows
- **Performance Tests**: Benchmark stuff like SIMD vs scalar implementations

#### Using the Test Script Directly
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

# Run specific tests
./tests/scripts/run_tests.sh unit options
./tests/scripts/run_tests.sh unit buffer_pool packet_queue

```

#### Windows Docker Testing
On Windows, since Criterion is POSIX-based, tests must be run in a Docker container. Use the PowerShell wrapper script:

```powershell
# Run all tests
./tests/scripts/run-docker-tests.ps1

# By default run-docker-tests.ps1 just calls run_tests.sh
# For documentation see above or check the script file
./tests/scripts/run-docker-tests.ps1 unit
./tests/scripts/run-docker-tests.ps1 unit options
./tests/scripts/run-docker-tests.ps1 unit buffer_pool packet_queue

# Compile with a different CMake build type (you may need to clean `./build_docker/`)
./tests/scripts/run-docker-tests.ps1 unit -BuildType release

# This script can also run clang-tidy static analysis
./tests/scripts/run-docker-tests.ps1 clang-tidy
./tests/scripts/run-docker-tests.ps1 clang-tidy lib/common.c

# INFO: powershell doesn't like when you pass -Verbose to a script - use -VerboseOutput
./tests/scripts/run-docker-tests.ps1 unit options -VerboseOutput

```

The Docker script automatically:
- Builds the test container if needed
- Mounts your source code for live testing
- Handles incremental builds
- Provides the same test interface as the native script

#### Manual Test Execution
You can also run individual test executables directly:
```bash
# Build the project first
cmake --preset debug && cmake --build --preset debug

# Run individual tests
build/bin/test_unit_mixer --verbose
build/bin/test_performance_ascii_simd --filter "*monochrome*"
```


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
- [ ] Square packing / grid packing / putting multiple squares into one square efficiently. For the multi-client grid layout system.
- [x] Multiple clients. Grid to display them.
- [x] Snapshot mode for clients with --snapshot to "take a photo" of a call and print it to the terminal or a file, rather than rendering video for a long time.
- [x] Audio mixing for multiple clients with compression and ducking.
- [ ] Color filters so you can pick a color for all the ascii so it can look like the matrix when you pick green (Gurpreet suggested).
- [x] Lock-free packet send queues.
- [x] Hardware-accelerated ASCII-conversion via SIMD.
- [x] Windows support.
- [x] Linux support.
- [x] Crypto.
- [ ] GPG key support for crypto (there's a bug upstream in libgcrypt).
= [ ] ascii-chat internet protocol rfc
= [ ] ascii-chat discovery service
= [ ] zalgo text generator
- [ ] v4l2 webcam images working.
- [ ] make more little todos from the github issues so they're tracked in the readme because i like the changelog (i can check with git when things are checked off)


## Notes

- **Note:** Colored frames are many times larger than monochrome frames due
to the ANSI color codes.

- We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
