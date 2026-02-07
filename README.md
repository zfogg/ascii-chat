# 💻📸 ascii-chat 🔡💬

Video chat in your terminal

🌐 **[ascii-chat.com](https://ascii-chat.com)** - Homepage, installation, and documentation

📚 **[Doxygen Documentation](https://zfogg.github.io/ascii-chat/)** - Developer API reference, architecture guides, and more

Probably the first command line video chat program (let me know if this isn't
true). Initial commits _November 20-24, 2013_, with
[@craigpalermo](https://github.com/craigpalermo), at some collegiate hackathon.

ascii-chat is a client-server application that operates over TCP/IP. It
supports color and audio and crypto and compression and multiple clients
and has many little features and options.

The client functions by simply printing text and terminal escape codes to your
terminal, so it works EVERYWHERE that terminals work: on rxvt-unicode in
OpenBox, in a Putty SSH session, in iTerm and Kitty.app on macOS, and
theoretically everywhere else terminals run. You just need a webcam.

ascii-chat even works in an initial unix login shell. You know, the shell that
runs 'startx' and launches your desktop environment so you can open a gui
terminal app like Konsole or Kitty or Alacritty in kde or xfce. You don't need a
desktop environment at all to video chat with ascii-chat. (\*)

🆕 We support UTF-8 now so it's not just ASCII anymore. However, the name is still ascii-chat.

🆕 Now 3+ simultaneous people can connect and the server will render the clients to each other as a grid, like Google Hangouts and Zoom calls do. See the **[Network Protocol docs](https://zfogg.github.io/ascii-chat/group__network.html#topic_network)**.

🆕 Audio is now supported - turn on your microphone and start talking! See the **[Audio System docs](https://zfogg.github.io/ascii-chat/group__audio.html#topic_audio)**. (TODO: buggy - needs work). Update: works with --file and --url.

🆕 --file and --url and YouTube support! Turning media via files and URLS into ascii art is now supported via ffmpeg, with special YouTube support via yt-dlp. You can now turn media from any format ffmpeg supports into ascii art, including animated gifs. You can use ascii-chat to watch or stream any youtube video or other files or internet video streams as ascii art by simply passing the file or url to ascii-chat with --file or --url, and you can livestream the result with audio into an ascii-chat call. This means you could stream a video from YouTube into a call and discuss it live with your friends, all as ascii art.

\(\*) Testing needed to verify a decent framerate.

## Animated Demonstrations

ascii-chat v0 from 2013:

![Animated demonstration: monochrome](https://i.imgur.com/E4OuqvX.gif)

ascii-chat v0.3.5 in 2025. Here are 3 clients connected to a single server, in a call:

![Animated demonstration: multi-client](https://media0.giphy.com/media/v1.Y2lkPTc5MGI3NjExMHFwczNjY3Fudndmb2kxajJxMmY0aG8ycHc5N29pbDh6czBrdXVreSZlcD12MV9pbnRlcm5hbF9naWZfYnlfaWQmY3Q9Zw/68F2r2pduHugH5sXa5/giphy.gif)

## Code Coverage

[![codecov](https://codecov.io/gh/zfogg/ascii-chat/graph/badge.svg?token=Nkt0GBDMIH)](https://codecov.io/gh/zfogg/ascii-chat)

[![codecov svg](https://codecov.io/gh/zfogg/ascii-chat/graphs/sunburst.svg?token=Nkt0GBDMIH)](https://codecov.io/gh/zfogg/ascii-chat)

## Table of Contents

- [💻📸 ascii-chat 🔡💬](#-ascii-chat-)
  - [Animated Demonstrations](#animated-demonstrations)
  - [Code Coverage](#code-coverage)
  - [Table of Contents](#table-of-contents)
  - [Get ascii-chat](#get-ascii-chat)
  - [Build From Source](#build-from-source)
  - [Usage](#usage)
  - [Cryptography](#cryptography)
  - [Environment Variables](#environment-variables)
  - [ascii-chat Internet Protocol (ACIP)](#ascii-chat-internet-protocol-acip)
  - [ascii-chat Discovery Service (ACDS)](#ascii-chat-discovery-service-acds)
  - [libasciichat](#libasciichat)
  - [Open Source](#open-source)
  - [TODO](#todo)
  - [Notes](#notes)

## Get ascii-chat

**macOS (Homebrew):**

```bash
# Add the tap
brew tap zfogg/ascii-chat

# Install runtime binary
brew install ascii-chat

# Install development libraries (headers, static/shared libs, docs)
brew install libasciichat
```

**Arch Linux (AUR):**

[![ascii-chat](https://img.shields.io/aur/version/ascii-chat?color=1793d1&label=ascii-chat&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/ascii-chat/)
[![libasciichat](https://img.shields.io/aur/version/libasciichat?color=1793d1&label=libasciichat&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/libasciichat/)
[![ascii-chat-git](https://img.shields.io/aur/version/ascii-chat-git?color=1793d1&label=ascii-chat-git&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/ascii-chat-git/)
[![libasciichat-git](https://img.shields.io/aur/version/libasciichat-git?color=1793d1&label=libasciichat-git&logo=arch-linux&style=for-the-badge)](https://aur.archlinux.org/packages/libasciichat-git/)

```bash
# Stable releases
paru -S ascii-chat           # Runtime binary
paru -S libasciichat         # Development libraries (headers, libs, docs)

# Latest git versions (recommended for development)
paru -S ascii-chat-git       # Runtime binary from git
paru -S libasciichat-git     # Development libraries from git
```

**All Platforms:**

- All downloads: [GitHub Releases](https://github.com/zfogg/ascii-chat/releases)
- Documentation: **[zfogg.github.io/ascii-chat](https://zfogg.github.io/ascii-chat/)** — API reference and developer guides
- Source installation: follow the [Build from source](#build-from-source) steps (or the **[Build System docs](https://zfogg.github.io/ascii-chat/group__build.html)**)

## Build From Source

> 📚 **Complete build guide: [Build System Documentation](https://zfogg.github.io/ascii-chat/group__build.html)** — CMake presets, configuration options, troubleshooting, and platform-specific details.

**Quick start:**

```bash
# 1. Clone the repository
git clone git@github.com:zfogg/ascii-chat.git
cd ascii-chat

# 2. Get submodules
git submodule update --init --recursive

# 3. Install dependencies (see supported platforms above)
./scripts/install-deps.sh      # Linux or macOS
# ./scripts/install-deps.ps1   # Windows

# 4. Build
cmake --preset default
cmake --build build

# 5. Run
./build/bin/ascii-chat server  # Start server
./build/bin/ascii-chat client  # Connect client (in another terminal)
```

For detailed build instructions, configuration options, and troubleshooting, see the **[Build System Documentation](https://zfogg.github.io/ascii-chat/group__build.html)**.

### Useful CMake Targets

Common development targets (run with `cmake --build build --target <name>`):

| Target                                                                    | Description                                                                                                                                                   |
| ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ascii-chat`                                                              | Build the main executable (default)                                                                                                                           |
| `shared-lib`                                                              | Build shared library (`libasciichat.so`/`.dylib`/`.dll`)                                                                                                      |
| `static-lib`                                                              | Build static library (`libasciichat.a`)                                                                                                                       |
| `tests`                                                                   | Build and run all unit and integration tests                                                                                                                  |
| `man1`                                                                    | Generate man page for `ascii-chat(1)`                                                                                                                         |
| `completions`                                                             | Generate shell completions (bash, fish, zsh, powershell)                                                                                                      |
| `docs`                                                                    | Generate API documentation with Doxygen                                                                                                                       |
| `docs-open`                                                               | Generate documentation and open in browser                                                                                                                    |
| `format`                                                                  | Format all C/C++ code with clang-format                                                                                                                       |
| `format-check`                                                            | Check code formatting without modifying files                                                                                                                 |
| `scan-build`                                                              | Run Clang static analyzer with detailed reports                                                                                                               |
| `scan-build-view`                                                         | Run Clang analyzer and open report in browser                                                                                                                 |
| `package`                                                                 | Create platform-specific installer packages                                                                                                                   |
| `package-tar`, `package-deb`, `package-rpm`, `package-dmg`, `package-zip` | Create specific package formats (`.tar.gz` on Unix/Linux, `.deb` on Debian/Ubuntu, `.rpm` on RedHat/Fedora, `.dmg` on macOS, `.zip` on Windows/all platforms) |

## Usage and --flags

ascii-chat uses a unified binary with four modes: `server`, `client`, `mirror`, and `discovery-service`.

**Get help:**

```bash
ascii-chat --help                      # Top-level help
ascii-chat server --help               # Server-specific help
ascii-chat client --help               # Client-specific help
ascii-chat mirror --help               # Mirror-specific help
ascii-chat discovery-service --help     # Discovery-service-specific help
```

**Read the installed manual:**

```bash
man 1 ascii-chat       # Main manual page
man 5 ascii-chat       # File formats
```

**Or create it from source:**

```bash
cmake --build build --target man1 --target man5
man build/share/man/man1/ascii-chat.1     # Main manual page
man build/share/man/man5/ascii-chat.5     # File formats
```

**Or view online:**

- [ascii-chat.com/man1](https://ascii-chat.com/man1)
- [ascii-chat.com/man5](https://ascii-chat.com/man5)

**Shell Completions:**

Tab completion is available for Bash, Fish, Zsh, and PowerShell. Completions are auto-generated from the options registry at build time.

**If installed via package manager (Homebrew, AUR, etc.):** Completions are already installed in standard directories. You may need to setup your shell's completion path:

```bash
# Zsh: Add Homebrew/Linuxbrew completions to FPATH (~/.zshrc)
export FPATH=$(brew --prefix)/share/zsh/site-functions:$FPATH

# Bash: if you have a custom BASH_COMPLETION_USER_DIR
export BASH_COMPLETION_USER_DIR=~/.bash-completions
ascii-chat --completions bash > $BASH_COMPLETION_USER_DIR/completions/completions/ascii-chat.bash
```

**If building from source or without a package manager:** Generate completions on-demand:

```bash
# Bash (~/.bashrc)
eval "$(ascii-chat --completions bash)"

# Fish (~/.config/fish/config.fish)
ascii-chat --completions fish | source

# Zsh (~/.zshrc)
eval "$(ascii-chat --completions zsh)"

# PowerShell ($PROFILE)
ascii-chat --completions powershell | Out-String | Invoke-Expression
```

## Cryptography

See **[ascii-chat.com/crypto](https://ascii-chat.com/crypto)** for complete cryptography documentation.

## Environment Variables

See **[ascii-chat.com/man1/#ENVIRONMENT](https://www.ascii-chat.com/man1#ENVIRONMENT)** for complete environment variable documentation.

## ascii-chat Internet Protocol (ACIP)

- 📡 **User Network Documentation: [ascii-chat.com/docs/network](https://www.ascii-chat.com/docs/network)**
- 📡 **Developer Protocol API Reference: [Doxygen network protocol documentation](https://zfogg.github.io/ascii-chat/group__network.html#topic_network)**

Regular users see the first link. Developers see the second link also.

### Philosophy

When I started building ascii-chat, no existing protocol fit the requirements for real-time terminal-based video conferencing. HTTP/WebSocket are too heavyweight and browser-focused. RTP/RTSP are designed for traditional video streams, not ASCII frames. VNC/RDP are for desktop sharing, not peer-to-peer.

So I designed ACIP. It's a binary protocol over TCP built for low-latency, encrypted, multi-client conference calling in terminals.

### Protocol Overview

ACIP is a binary packet protocol over TCP. It starts with a TCP handshake and capability negotiation, followed by a cryptographic handshake using X25519 Diffie-Hellman key exchange with Ed25519 authentication. Clients send their terminal dimensions, color support, and audio capabilities. The server sends session info, connected clients, and grid layout.

All packets are encrypted with XSalsa20-Poly1305 (AEAD cipher). Ephemeral session keys provide perfect forward secrecy. Optional SSH/GPG key verification prevents MITM attacks. Session keys rotate periodically for long-lived connections.

Clients must complete the crypto handshake before sending media. TCP guarantees packet order, and ACIP relies on this. Senders adapt to slow receivers via TCP flow control. Hardware-accelerated CRC32 checksums detect corruption.

Single packets contain complete ASCII/image frames (no app-layer fragmentation). Multiple audio samples are bundled for efficiency. Frames use zstd compression (configurable level 1-9). Audio uses Opus encoding (optional, can send raw PCM). Comprehensive packet tracing is available in debug builds. Error signaling will be added to the main protocol (currently only ACDS has proper error packets). Rate limiting and QoS are planned.

### Why ACIP

The protocol is efficient (20-byte headers, binary), reliable (TCP handles packet loss/ordering), encrypted by default, and extensible (new packet types don't break old clients). It understands terminal capabilities like color depth and dimensions. The server mixes video/audio from multiple clients. Implementation is ~4-5k lines of C in `lib/network/`.

## ascii-chat Discovery Service (ACDS)

> 🔍 **ACDS Documentation: [Discovery Service Reference](https://zfogg.github.io/ascii-chat/group__module__acds.html#topic_acds)**
>
> 🔑 **Official ACDS Server: discovery-service.ascii-chat.com:27225** (trusted by default)
>
> 📄 **Public Keys: [discovery.ascii-chat.com](https://discovery.ascii-chat.com)** — Server authentication keys

### Philosophy

**The Problem:** Running a video chat server should be as easy as opening a Zoom meeting. But networking is hard. Most home routers block incoming connections, requiring manual UPnP/NAT-PMP configuration. You need to know your public IP and share it with others. Symmetric NAT makes peer-to-peer connections nearly impossible. Corporate/university networks often block non-standard ports.

**The ACDS Solution:** Share a three-word session string like `purple-mountain-lake` and people can join. No IP addresses, no port forwarding, no network configuration.

### How ACDS Works

ACDS is a rendezvous server that helps clients find each other. The server registers with ACDS and gets a memorable session string (e.g., `happy-sunset-ocean`). ACDS attempts NAT traversal using UPnP, NAT-PMP, and WebRTC ICE/STUN/TURN. The server shares the session string with potential clients.

When a client queries ACDS with the session string, ACDS returns connection info: IP address(es), ports, NAT type, and relay options. The client tries direct TCP first (if UPnP/NAT-PMP succeeded, best latency), then WebRTC DataChannel (P2P via ICE/STUN, good latency, works behind NAT), and finally TURN relay as a fallback (higher latency, always works).

It all happens automatically. No user intervention required.

### mDNS Support (No Server Required!)

ACDS also supports **local network discovery via mDNS** (multicast DNS). On the same LAN, you can:

```bash
# Server announces on local network
ascii-chat server --mdns

# Client discovers servers automatically
ascii-chat client --mdns
```

This enables local sessions without any configuration - good for office environments, conferences, or home networks where you don't need internet-wide discovery.

### Session Strings

ACDS generates memorable session identifiers from a curated word list. The format is `adjective-noun-noun` (e.g., `bright-forest-river`), with 16.7M+ possible combinations for collision resistance. They're easy to speak over the phone, remember briefly, and type without errors. Session strings expire when the server disconnects.

### NAT Traversal Technologies

ACDS leverages multiple NAT traversal techniques. UPnP (Universal Plug and Play) works on ~70% of home routers with automatic port forwarding, enabling direct TCP connections with lowest latency. NAT-PMP (NAT Port Mapping Protocol) is Apple's alternative to UPnP, common on Apple routers and some enterprise gear, also enabling direct TCP connections. WebRTC (ICE/STUN/TURN) uses STUN to discover public IP/port mapping, ICE to negotiate P2P connections through NAT, and TURN as a relay fallback that always succeeds. Higher overhead but works in 99% of networks.

### Network Error Handling

ACDS implements **proper error signaling** that will eventually propagate to the main protocol:

- `ERROR_SESSION_NOT_FOUND` - Invalid session string
- `ERROR_SESSION_FULL` - Server at max capacity
- `ERROR_AUTHENTICATION_FAILED` - Key/password mismatch
- `ERROR_NETWORK_UNREACHABLE` - Cannot establish connection
- `ERROR_PROTOCOL_VERSION_MISMATCH` - Client/server version incompatibility

Future enhancement: ACIP will adopt this error framework for comprehensive error handling across the stack.

### Privacy & Security

**ACDS never sees your data:**

- Only exchanges connection metadata (IP addresses, ports)
- All media flows **directly between peers** using ACIP encryption
- ACDS doesn't decrypt, proxy, or store media content

**Official server (trusted by default):**

- The official ACDS server runs at **discovery-service.ascii-chat.com:27225**
- Its Ed25519 public keys available at **[discovery.ascii-chat.com](https://discovery.ascii-chat.com)**
- Manual verification via SSH and GPG fingerprints on the website

**Session database:**

- SQLite database stores active sessions (IP, port, timestamp, session string)
- Automatic cleanup of expired sessions
- Optional: private ACDS instances for corporate deployments

### Why ACDS

"Join happy-sunset-ocean" is easier than "Connect to 73.251.42.118:27224". Works behind firewalls without manual configuration. Tries the best connection first, falls back gracefully. mDNS support for LANs without a server. You can run your own ACDS server for privacy.

## libasciichat

ascii-chat is built on a modern, reusable C library called **libasciichat** that can be embedded in other projects. The library provides cross-platform code for developing ascii-chat internet protocol (ACIP) applications. You can install libasciichat as a development dependency and use to build other ACIP applications.

**What's in the library:**

- **Network Protocol**: Full implementation of the ascii-chat internet protocol with encrypted packet exchange, lossless frame compression (zstd), and audio codec integration (Opus). See the protocol reference in the docs.
- **Image Processing**: The `video` module converts images and videos to ASCII art with hardware acceleration via SIMD (AVX2, NEON, SSE) for 1-4x performance gains. Includes grid layout algorithms for multi-client rendering.
- **Platform Abstraction**: Write once, run anywhere. Cross-platform abstractions for threads, mutexes, read-write locks, condition variables, sockets, and terminal I/O that work identically on Windows, macOS, and Linux.
- **Media Support**: Audio capture, mixing, and playback via PortAudio; webcam integration with V4L2 (Linux), AVFoundation (macOS), and Media Foundation (Windows); frame buffering and synchronization. File media support via ffmpeg.
- **Cryptography**: End-to-end encryption with libsodium (X25519 key exchange, XSalsa20-Poly1305 AEAD, Ed25519 signatures) and SSH key authentication with agent support.
- **Debugging & Profiling**: Built-in memory leak detection with source file/line tracking, lock contention analysis, AddressSanitizer integration, and comprehensive logging infrastructure.
- **Memory Management**: High-performance buffer pooling, lock-free ring buffers, and thread-safe packet queues designed for real-time video/audio applications.

**Install libasciichat:**

```bash
# macOS (Homebrew)
brew install libasciichat

# Arch Linux (AUR)
paru -S libasciichat       # Stable release
paru -S libasciichat-git   # Latest from git
```

The library headers, static and shared libraries, and API documentation are included. Installation also provides CMake config files (`asciichatConfig.cmake`) and pkg-config metadata (`libasciichat.pc`) for integration into your own projects.

**Using libasciichat in your CMake project:**

```cmake
find_package(asciichat REQUIRED)
target_link_libraries(your_project asciichat::asciichat)
```

**Using libasciichat with pkg-config:**

```bash
pkg-config --cflags --libs libasciichat
```

See the online **[API documentation](https://zfogg.github.io/ascii-chat/)** for detailed function references and architecture guides.

## Open Source

ascii-chat is an open source project with an MIT license, meaning you can and should use our code to make cool stuff. Follow through this Open Source section of the README and you'll be compiling against libasciichat in no time.

> 💡 **For detailed API documentation**, see the **[online docs](https://zfogg.github.io/ascii-chat/)** — includes function references, architecture diagrams, and module documentation generated from source comments.

The first thing you need to do is get the dependencies. Some of them are git submodules of the repo. When you clone the repo get the submodules too.

```bash
git submodule init
git submodule update --recursive
```

Now let's list out and talk about the dependencies before we install them.

### Dependencies

> 📦 **Complete guide: [Build System / Dependencies](https://zfogg.github.io/ascii-chat/group__dependencies.html)**

ascii-chat is built on operating system code and several libraries.

#### List of Dependencies and What We Use Them For

- [**musl libc**](https://musl.libc.org/) - Static builds (Linux releases)
- [**mimalloc**](https://github.com/microsoft/mimalloc) - High-performance memory allocator (release builds)
- [**PortAudio**](http://www.portaudio.com/) - Cross-platform audio I/O
- [**tomlc17**](https://github.com/cktan/tomlc17) - ascii-chat has TOML config files
- [**uthash**](https://troydhanson.github.io/uthash/) - ascii-chat uses hash tables normally for a few different things
- [**utf8proc**](https://github.com/JuliaStrings/utf8proc) - UTF-8 support even though it's named ascii-chat
- [**PCRE2**](https://www.pcre.org/) - Regular expressions for strings; validation and parsing and search
- [**libsodium**](https://libsodium.org/) - End-to-end encryption and authentication
- [**libsodium-bcrypt-pbkdf**](https://github.com/imaami/libsodium-bcrypt-pbkdf) - Decrypt password-protected SSH Ed25519 keys
- [**BearSSL**](https://bearssl.org/) - HTTPS client for GitHub/GitLab key fetching; AES for SSH key decryption
- [**zstd**](https://facebook.github.io/zstd/) - Network data compression
- [**Opus**](https://opus-codec.org/) - Audio codec for low-latency voice transmission
- [**WebRTC AEC3**](https://github.com/nicnacnic/webrtc_AEC3) - WebRTC acoustic echo cancellation so you can talk to each other without hearing yourself
- [**Sokol**](https://github.com/floooh/sokol) - Header-only cross-platform utilities (timing, audio)
- [**SQLite3**](https://www.sqlite.org/) - Database for discovery service session storage
- [**libdatachannel**](https://github.com/paullouisageneau/libdatachannel) - WebRTC DataChannels
- [**libwebsockets**](https://libwebsockets.org/) - WebSocket server for browser clients
- [**miniupnpc**](https://miniupnp.tuxfamily.org/) - Opens ports in your router to ascii-chat so clients can connect
- [**FFmpeg**](https://ffmpeg.org/) - Media support for `--file` and `--url`. `cat` images or videos into ascii-chat
- [**yt-dlp**](https://github.com/yt-dlp/yt-dlp) - YouTube.com support for `--url` (optional)
- [**ssh-agent**](https://linux.die.net/man/1/ssh-agent) - SSH key agent authentication (optional)
- [**gpg-agent**](https://linux.die.net/man/1/gpg-agent) - GPG key agent authentication (optional)

#### Install Dependencies

**Supported platforms:**

- macOS (Homebrew)
- Linux: Debian/Ubuntu (apt), Fedora/RedHat/CentOS (yum), Arch (pacman)
- Windows (vcpkg via PowerShell)

**Linux or macOS:**

```bash
./scripts/install-deps.sh
```

**Windows:**

```powershell
./scripts/install-deps.ps1
```

For detailed installation instructions and troubleshooting, see the **[Build System / Dependencies](https://zfogg.github.io/ascii-chat/group__dependencies.html)** documentation.

### What is musl and mimalloc?

**musl libc**: A lightweight, fast, and simple C standard library alternative to glibc. `Release` builds (`cmake --preset release`) use musl to create **statically linked binaries** that have no external dependencies - perfect for deployment as they work on any Linux system without requiring specific libraries to be installed.

**mimalloc**: Microsoft's high-performance memory allocator. Release builds use mimalloc instead of the system allocator for better performance. It provides:

- Up to 2x faster allocation/deallocation
- Better memory locality and cache performance
- Lower memory fragmentation
- Optimized for multi-threaded workloads

### Development Tools & Configuration

For development tools (formatting, linting, documentation generation, packaging) and CMake configuration options (presets, sanitizers, build types, etc.), see the comprehensive **[Build System Documentation](https://zfogg.github.io/ascii-chat/group__build.html)**.

### Documentation

📖 **[Online Documentation](https://zfogg.github.io/ascii-chat/)** — API reference, architecture guides, and module documentation

The documentation is automatically generated from source code using Doxygen and published to GitHub Pages. For local documentation builds, see the **[Build System Documentation](https://zfogg.github.io/ascii-chat/group__build.html)**.

### Testing

> 🧪 **Full documentation: [Testing Framework](https://zfogg.github.io/ascii-chat/group__testing.html)**

#### Testing Framework

- **Framework**: [libcriterion](https://criterion.readthedocs.io/en/master/)
- **Coverage**: Code coverage reports generated in CI
- **Performance**: SIMD performance tests with aggressive speedup expectations (1-4x)
- **Memory Checking**: Comprehensive sanitizer support via `-b debug` for detecting memory issues, undefined behavior, and more

Tests are run using CMake's ctest tool, which integrates with Criterion for parallel execution and XML output.

#### Quick Start

- Have the dependencies installed.
- Choose:
  1. Linux or macOS: Use ctest directly
  2. Windows: Use Docker: `./tests/scripts/run-docker-tests.ps1`

#### Test Types

- **Unit Tests**: Test individual components in isolation
- **Integration Tests**: Test component interactions and full workflows
- **Performance Tests**: Benchmark stuff like SIMD vs scalar implementations

#### Running Tests with ctest

```bash
# Build tests first
cmake --build build --target tests

# Run all tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific test categories using labels
ctest --test-dir build --label-regex "^unit$" --output-on-failure
ctest --test-dir build --label-regex "^integration$" --output-on-failure
ctest --test-dir build --label-regex "^performance$" --output-on-failure

# Run specific tests by name pattern
ctest --test-dir build -R "buffer_pool" --output-on-failure

# List available tests
ctest --test-dir build -N
```

#### Windows Docker Testing

On Windows, since Criterion is POSIX-based, tests must be run in a Docker container. Use the PowerShell wrapper script:

```powershell
# Run all tests
./tests/scripts/run-docker-tests.ps1

# Run specific test category
./tests/scripts/run-docker-tests.ps1 unit
./tests/scripts/run-docker-tests.ps1 integration
./tests/scripts/run-docker-tests.ps1 performance

# Run tests matching a pattern
./tests/scripts/run-docker-tests.ps1 -Filter "*buffer*"

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
cmake --preset debug && cmake --build build

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
- [ ] Fix v4l2 usage because the ascii rendered looks corrupted on Linux.
- [x] Crypto.
- [x] GPG key support for crypto (Ed25519 via gpg-agent).
- [ ] zalgo text generator
- [x] v4l2 webcam images working.
- [ ] make more little todos from the github issues so they're tracked in the readme because i like the changelog (i can check with git when things are checked off)
- [ ] submit an ascii-chat internet protocol (ACIP) ietf rfc
- [ ] ascii-chat discovery service
- [ ] submit an ascii-chat discovery service (ACDS) ietf rfc
- [ ] ascii-chat official homebrew bottles

## Notes

- **Note:** Colored frames are many times larger than monochrome frames due
  to the ANSI color codes.

- We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
