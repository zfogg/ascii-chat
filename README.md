# 💻📸 ascii-chat 🔡💬

Video chat in your terminal

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

🆕 Audio is now supported - turn on your microphone and start talking! See the **[Audio System docs](https://zfogg.github.io/ascii-chat/group__audio.html#topic_audio)**. (TODO: buggy - needs work, audio isn't crisp)

📚 **[Read the Documentation](https://zfogg.github.io/ascii-chat/)** - Full API reference, architecture guides, and more.

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
  - [Command Line Flags](#command-line-flags)
    - [Client Mode Options](#client-mode-options)
    - [Server Mode Options](#server-mode-options)
    - [Mirror Mode Options](#mirror-mode-options)
  - [Cryptography](#cryptography)
    - [Authentication Options](#authentication-options)
    - [Usage Examples](#usage-examples)
  - [Environment Variables](#environment-variables)
    - [Security Variables](#security-variables)
    - [Terminal Variables (Used for Display Detection)](#terminal-variables-used-for-display-detection)
    - [POSIX-Specific Variables](#posix-specific-variables)
    - [Windows-Specific Variables](#windows-specific-variables)
    - [Development/Testing Variables](#developmenttesting-variables)
  - [libasciichat](#libasciichat)
  - [Open Source](#open-source)
    - [Dependencies](#dependencies)
      - [List of Dependencies and What We Use Them For](#list-of-dependencies-and-what-we-use-them-for)
      - [Operating System APIs](#operating-system-apis)
      - [Install Dependencies](#install-dependencies)
    - [What is musl and mimalloc?](#what-is-musl-and-mimalloc)
    - [Development Tools & Configuration](#development-tools--configuration)
    - [Documentation](#documentation)
    - [Testing](#testing)
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

```bash
# Stable releases
paru -S ascii-chat           # Runtime binary
paru -S libasciichat         # Development libraries (headers, libs, docs)

# Latest git versions (recommended for development)
paru -S ascii-chat-git       # Runtime binary from git
paru -S libasciichat-git     # Development libraries from git
```

**All Platforms:**

- Latest release: [v0.4.12](https://github.com/zfogg/ascii-chat/releases/tag/v0.4.12)
- All downloads: [GitHub Releases](https://github.com/zfogg/ascii-chat/releases)
- Documentation: **[zfogg.github.io/ascii-chat](https://zfogg.github.io/ascii-chat/)** — API reference and developer guides
- Source installation: see the [Dependencies](#dependencies) section below, then follow the [Build from source](#build-from-source) steps (or the **[Build System docs](https://zfogg.github.io/ascii-chat/group__build.html)**)

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

## Usage

ascii-chat uses a unified binary with three modes: `server`, `client`, and `mirror`.

**Start a server** and wait for client connections:

```bash
# NOTE: on Windows the filename is ascii-chat.exe
ascii-chat [binary-options...] server [server-options...]
```

**Connect to a server** as a client:

```bash
ascii-chat [binary-options...] client [<address>] [client-options...]
```

**View your local webcam** without a network connection (mirror mode):

```bash
ascii-chat [binary-options...] mirror [mirror-options...]
```

**Get help** for any mode:

```bash
ascii-chat --help             # Top-level help
ascii-chat server --help      # Server-specific help
ascii-chat client --help      # Client-specific help
ascii-chat mirror --help      # Mirror-specific help
```

**Man Page:**

ascii-chat includes a man page with comprehensive documentation. When installed via Homebrew or a package manager:

```bash
man ascii-chat
```

From the build directory (after running `cmake --build build --target docs`):

```bash
man build/docs/ascii-chat.1
```

## Command Line Flags

### Binary-Level Options

These options apply to all modes (server, client, mirror) and must be specified **before** the mode:

**Configuration:**

- `--config FILE`: Load configuration from TOML file
- `--config-create [PATH]`: Create default configuration file and exit

**Logging:**

- `-L --log-file FILE`: Redirect logs to file (default: server.log/client.log/mirror.log based on mode)
- `--log-level LEVEL`: Set log level: dev, debug, info, warn, error, fatal (default: info in release, debug in debug builds)
- `-V --verbose`: Increase verbosity (stackable: -V, -VV, -VVV for more detail)
- `-q --quiet`: Disable console logging (logs only to file)

**Information:**

- `--version`: Display version information
- `--help`: Show help message

**Example:**

```bash
# Binary options come before the mode
ascii-chat -V --log-level debug --log-file /tmp/debug.log client
ascii-chat --config ~/my-config.toml server
```

### Client Mode Options

Run `ascii-chat client --help` to see all client options.

**Connection (positional argument):**

Client accepts 0-1 positional argument for server address:

- `[address][:port]`: Server address with optional port
  - `address`: IPv4, IPv6, or hostname (default: localhost)
  - `:port`: Optional port suffix (default: 27224)

Examples:

```bash
ascii-chat client                      # Connect to localhost:27224
ascii-chat client 192.168.1.1          # Connect to 192.168.1.1:27224
ascii-chat client example.com:8080     # Connect to example.com:8080
ascii-chat client [::1]:8080           # Connect to IPv6 ::1:8080
```

**Connection (flags):**

- `-p --port PORT`: TCP port (default: 27224) - conflicts with port in positional argument
- `--reconnect VALUE`: Automatic reconnection behavior: `off`, `auto`, or number 1-999 (default: auto)

**Terminal Dimensions:**

- `-x --width WIDTH`: Terminal width in characters (auto-detected by default)
- `-y --height HEIGHT`: Terminal height in characters (auto-detected by default)
- `--stretch`: Stretch video to fit without preserving aspect ratio

**Webcam Options:**

- `-c --webcam-index INDEX`: Webcam device index (0-based, default: 0)
- `-f --webcam-flip`: Toggle horizontal flip of webcam image (default: flipped)
- `--test-pattern`: Use test pattern instead of real webcam (for debugging/testing)
- `--list-webcams`: List available webcam devices and exit

**Display & Color:**

- `--color-mode MODE`: Color modes: auto, none, 16, 256, truecolor (default: auto)
- `-M --render-mode MODE`: Render modes: foreground (fg), background (bg), half-block (default: foreground)
- `-P --palette TYPE`: ASCII palette: standard, blocks, digital, minimal, cool, custom (default: standard)
- `-C --palette-chars CHARS`: Custom palette characters (implies --palette=custom)
- `--show-capabilities`: Display detected terminal color capabilities and exit
- `--utf8`: Force enable UTF-8/Unicode support

**Audio Options:**

- `-A --audio`: Enable audio capture and playback
- `--microphone-index INDEX`: Microphone device index (-1 for system default)
- `--speakers-index INDEX`: Speakers device index (-1 for system default)
- `--list-microphones`: List available audio input devices and exit
- `--list-speakers`: List available audio output devices and exit
- `--audio-analysis`: Enable audio analysis for debugging audio quality issues
- `--no-audio-playback`: Disable speaker playback while keeping received audio recording

**Performance:**

- `--fps FPS`: Desired frame rate, 1-144 (default: 60)
- `--compression-level LEVEL`: zstd compression level 1-9 (default: 1, fastest)
- `--no-compress`: Disable video frame compression and audio encoding entirely
- `--encode-audio`: Force enable Opus audio encoding (overrides --no-compress)
- `--no-encode-audio`: Disable Opus audio encoding, send raw audio samples

**Snapshot Mode:**

- `-S --snapshot`: Capture single frame from server and exit (useful for scripting, CI/CD)
- `-D --snapshot-delay SECONDS`: Delay in seconds before capturing snapshot (default: 3.0-4.0 for webcam warmup)
- `--strip-ansi`: Remove all ANSI escape codes from output (plain ASCII only)

**Encryption:**

- `-E --encrypt`: Enable packet encryption (default: enabled if keys are available)
- `-K --key FILE`: SSH/GPG key file for authentication (supports /path/to/key, github:user, gitlab:user, or 'ssh' for auto-detect)
- `--password PASSWORD`: Password for connection encryption
- `-F --keyfile FILE`: Alternative way to specify key file (alias for --key)
- `--no-encrypt`: Disable encryption (for local testing)
- `--server-key KEY`: Expected server public key for identity verification (prevents MITM attacks)

### Server Mode Options

Run `ascii-chat server --help` to see all server options.

**Network Binding (positional arguments):**

Server accepts 0-2 positional arguments for bind addresses:

- 0 arguments: bind to defaults (127.0.0.1 and ::1)
- 1 argument: bind to this IPv4 OR IPv6 address
- 2 arguments: bind to both (must be one IPv4 and one IPv6)

Examples:

```bash
ascii-chat server                      # Bind to 127.0.0.1 and ::1
ascii-chat server 0.0.0.0              # Bind to all IPv4 interfaces
ascii-chat server ::                   # Bind to all IPv6 interfaces
ascii-chat server 0.0.0.0 ::           # Bind to all interfaces (IPv4 and IPv6)
ascii-chat server 127.0.0.1 ::1        # Bind to localhost (IPv4 and IPv6)
```

**Network Binding (flags):**

- `-p --port PORT`: TCP port to listen on (default: 27224)
- `--max-clients N`: Maximum concurrent client connections, 1-32 (default: 10)

**Display & Palette:**

- `-P --palette TYPE`: ASCII palette: standard, blocks, digital, minimal, cool, custom (default: standard)
- `-C --palette-chars CHARS`: Custom palette characters (implies --palette=custom)

**Performance:**

- `--compression-level LEVEL`: zstd compression level 1-9 (default: 1, fastest)
- `--no-compress`: Disable video frame compression entirely
- `--encode-audio`: Force enable Opus audio encoding (overrides --no-compress)
- `--no-encode-audio`: Disable Opus audio encoding, send raw audio samples
- `--no-audio-mixer`: Disable audio mixer, send silence instead of mixing (debug only)

**Encryption:**

- `-E --encrypt`: Enable packet encryption (default: enabled if keys are available)
- `-K --key FILE`: SSH/GPG key file for authentication: /path/to/key, github:user, gitlab:user, or 'ssh' for auto-detect
- `--password PASSWORD`: Password for connection encryption
- `-F --keyfile FILE`: Alternative way to specify key file (alias for --key)
- `--no-encrypt`: Disable encryption (for local testing)
- `--client-keys FILE`: File containing allowed client public keys for authentication (whitelist, one per line in authorized_keys format)

### Mirror Mode Options

Run `ascii-chat mirror --help` to see all mirror mode options.

Mirror mode displays your local webcam as ASCII art without any network connection - perfect for testing your webcam, palette settings, and terminal rendering.

**Terminal Dimensions:**

- `-x --width WIDTH`: Terminal width in characters (auto-detected by default)
- `-y --height HEIGHT`: Terminal height in characters (auto-detected by default)
- `--stretch`: Stretch video to fit without preserving aspect ratio

**Webcam Options:**

- `-c --webcam-index INDEX`: Webcam device index (0-based, default: 0)
- `-f --webcam-flip`: Toggle horizontal flip of webcam image (default: flipped)
- `--test-pattern`: Use test pattern instead of real webcam
- `--list-webcams`: List available webcam devices and exit

**Display & Color:**

- `--color-mode MODE`: Color modes: auto, none, 16, 256, truecolor (default: auto)
- `-M --render-mode MODE`: Render modes: foreground (fg), background (bg), half-block (default: foreground)
- `-P --palette TYPE`: ASCII palette: standard, blocks, digital, minimal, cool, custom (default: standard)
- `-C --palette-chars CHARS`: Custom palette characters (implies --palette=custom)
- `--show-capabilities`: Display detected terminal color capabilities and exit
- `--utf8`: Force enable UTF-8/Unicode support

**Performance:**

- `--fps FPS`: Desired frame rate, 1-144 (default: 60)

**Snapshot Mode:**

- `-S --snapshot`: Capture single frame and exit (useful for testing palettes)
- `-D --snapshot-delay SECONDS`: Delay in seconds before capturing snapshot (default: 3.0-4.0 for webcam warmup)
- `--strip-ansi`: Remove all ANSI escape codes from output (plain ASCII only)

**Example:**

```bash
# View webcam with custom palette
ascii-chat mirror --palette blocks

# Test truecolor rendering with half-block mode
ascii-chat mirror --color-mode truecolor --render-mode half-block

# Capture a single snapshot with debug logging
ascii-chat -V --log-level debug mirror --snapshot
```

## Cryptography

> 🔐 **Protocol details: [Cryptographic Handshake Documentation](https://zfogg.github.io/ascii-chat/group__handshake.html#topic_handshake)**

ascii-chat supports **end-to-end encryption** using libsodium with Ed25519 key authentication and X25519 key exchange.

ascii-chat's crypto works like your web browser's HTTPS: the client and server perform the Diffie-Hellman exchange to establish secure communication with ephemeral keys every connection. HTTPS depends on certificates tied to DNS names with a certificate authority roots build into the operating system, but ascii-chat is built on TCP so DNS doesn't work for us to secure our servers. ascii-chat users need to verify their server's public keys manually until ACDS (ascii-chat discovery service) is built.

### Authentication Options

**SSH/GPG Key Authentication** (`--key`):

- Use your existing SSH Ed25519 keys for authentication
- Use GPG Ed25519 keys via gpg-agent (no passphrase prompts)
- Supports encrypted SSH keys (prompts for passphrase or uses ssh-agent)
- Supports GitHub public keys with `--client-keys github:username` and `--server-key github:username`
- GPG key formats: `gpg:KEYID` where KEYID is 8, 16, or 40 hex characters (short/long/full fingerprint)

**Password-Based Encryption** (`--password`):

- Simple password string for encrypting connections
- Can be combined with `--key` for dual authentication + encryption

**Ephemeral Keys** (default):

- When no authentication is provided, generates temporary keypair for the session

### Usage Examples

```bash
# SSH key authentication (prompts for passphrase if encrypted)
ascii-chat client --key ~/.ssh/id_ed25519

# GPG key authentication (uses gpg-agent, no passphrase prompt)
ascii-chat server --key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3

# Password-based encryption
ascii-chat server --password "hunter2"

# Both SSH key + password (double security)
ascii-chat client --key ~/.ssh/id_ed25519 --password "extra_encryption"

# Disable encryption (for local testing)
ascii-chat server --no-encrypt

# Server key verification with SSH (client verifies server identity)
ascii-chat client --key ~/.ssh/id_ed25519 --server-key ~/.ssh/server1.pub
# This .pub file format is standard OpenSSH public key format (ssh-ed25519).

# Server key verification with GPG (client verifies server identity)
ascii-chat client --server-key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3

# Server key verification using GitHub GPG keys (fetches server's GPG keys from GitHub)
ascii-chat client --server-key github:zfogg.gpg

# Server key verification using GitLab GPG keys
ascii-chat client --server-key gitlab:username.gpg

# Client key whitelisting (server only accepts specific clients)
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys allowed_clients.txt
# This .txt file contains multiple .pub file contents, 1 per line, where each line is a client key that is allowed to connect to the server.

# GitHub GPG key whitelisting (fetch client's public GPG keys from GitHub)
ascii-chat server --key gpg:MYKEYID --client-keys github:zfogg.gpg
# Server fetches all GPG public keys from https://github.com/zfogg.gpg and whitelists them
# Client must authenticate with their GPG key:
ascii-chat client --key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3 --server-key gpg:MYKEYID

# GitLab GPG key whitelisting (same but from GitLab)
ascii-chat server --key gpg:MYKEYID --client-keys gitlab:username.gpg

# Combine all three for maximum security!
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys ~/.ssh/client1.pub --password "password123"
# You need to know (1) the server public key and (2) the password before connecting, and the server needs to know (3) your public key and (4) the same password.

# GPG key with password for extra security
ascii-chat server --key gpg:7FE90A79F2E80ED3 --password "password123"
```

## Environment Variables

ascii-chat uses several environment variables for configuration and security
controls. These variables can be set to modify the program's behavior without
changing command-line arguments.

### Security Variables

- `ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK`

  - **Purpose**: Disables host identity verification (known_hosts checking)
  - **Values**: `1` (enable), unset or any other value (disable, default)
  - **⚠️ DANGER**: This completely bypasses security checks and makes connections vulnerable to man-in-the-middle attacks

- `SSH_AUTH_SOCK`

  - **Purpose**: SSH agent socket for secure key authentication
  - **Values**: Path to SSH agent socket (e.g., `/tmp/ssh-XXXXXX/agent.12345`)
  - **Security**: ✅ **Secure** - uses SSH agent for key management
  - **When to use**: Preferred method for SSH key authentication (automatically detected)
  - **Used for**: SSH key authentication without storing passphrases in environment

- `ASCII_CHAT_KEY_PASSWORD`

  - **Purpose**: Provides key passphrase for encrypted SSH or GPG keys
  - **Values**: The passphrase string for your encrypted key
  - **Security**: ⚠️ **Sensitive data** - contains your key passphrase - prefer ssh-agent/gpg-agent over this (we support both)
  - **When to use**: When using encrypted keys and you want to avoid interactive passphrase prompts

### Terminal Variables (Used for Display Detection)

- `TERM`

  - **Purpose**: Terminal type detection for display capabilities
  - **Usage**: Automatically set by terminal emulators
  - **Used for**: Determining color support, character encoding, and display features

- `COLORTERM`

  - **Purpose**: Additional terminal color capability detection
  - **Usage**: Automatically set by modern terminal emulators
  - **Used for**: Enhanced color support detection beyond `TERM`

- `LANG`, `LC_ALL`, `LC_CTYPE`

  - **Purpose**: Locale and character encoding detection
  - **Usage**: Automatically set by system locale
  - **Used for**: UTF-8 support detection and character encoding

- `TTY`

  - **Purpose**: Terminal device detection
  - **Usage**: Automatically set by terminal sessions
  - **Used for**: Determining if running in a real terminal vs. script

- `LINES`, `COLUMNS`
  - **Purpose**: Terminal size detection for display dimensions
  - **Usage**: Automatically set by terminal emulators
  - **Used for**: Auto-detecting optimal video dimensions

### POSIX-Specific Variables

- `USER`

  - **Purpose**: Username detection for system identification on POSIX systems
  - **Usage**: Automatically set by POSIX systems
  - **Used for**: System user identification and logging

- `HOME`

  - **Purpose**: Determines user home directory for configuration files on POSIX systems
  - **Usage**: Automatically detected by the system
  - **Used for**:
    - SSH key auto-detection (`~/.ssh/`)
    - Configuration file paths (`~/.ascii-chat/`)
    - Path expansion with `~` prefix

### Windows-Specific Variables

- `USERNAME`

  - **Purpose**: Username detection for system identification on Windows
  - **Usage**: Automatically set by Windows system
  - **Used for**: System user identification and logging

- `USERPROFILE`

  - **Purpose**: Determines user home directory for configuration files on Windows
  - **Usage**: Automatically detected by the Windows system
  - **Used for**:
    - SSH key auto-detection (`~/.ssh/`)
    - Configuration file paths (`~/.ascii-chat/`)
    - Path expansion with `~` prefix

- `_NT_SYMBOL_PATH`

  - **Purpose**: Windows debug symbol path for crash analysis
  - **Usage**: Automatically set by Windows debug tools
  - **Used for**: Enhanced crash reporting and debugging

### Development/Testing Variables

- `CI`

  - **Purpose**: Continuous Integration environment detection
  - **Values**: Any non-empty value indicates CI environment
  - **Used for**: Adjusting test behavior and terminal detection in automated environments

- `TESTING`, `CRITERION_TEST`

  - **Purpose**: Test environment detection
  - **Values**: Any non-empty value indicates test environment
  - **Used for**: Reducing test data sizes and adjusting performance expectations

- `WEBCAM_DISABLED`

  - **Purpose**: Automatically enables test pattern mode without requiring `--test-pattern` flag
  - **Values**: `1`, `true`, `yes`, or `on` (case-insensitive for string values)
  - **Used for**: CI/CD pipelines and testing environments where no physical webcam is available
  - **Effect**: Sets `opt_test_pattern = true`, causing the client to use a generated test pattern instead of webcam input

## ascii-chat internet protocol (acip)

ascii-chat's protocol is called acip, and it's currently used and implemented by three three ascii-chat applications: ascii-chat client, ascii-chat server, and ascii-chat discovery service (acds). The protocol is designed in a future-proof and extensible way. The protocol is defined and implemented in libasciichat code in `lib/network` and used by the programs of this repo.

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

- [**musl libc**](https://musl.libc.org/) - Static builds for Linux releases (optional for development)
- [**mimalloc**](https://github.com/microsoft/mimalloc) - High-performance memory allocator (optional for development)
- [**PortAudio**](http://www.portaudio.com/) - Cross-platform audio I/O for bidirectional voice communication
- [**tomlc17**](https://github.com/cktan/tomlc17) - TOML config file parsing (`~/.config/ascii-chat/config.toml`)
- [**uthash**](https://troydhanson.github.io/uthash/) - Hash tables for fast O(1) client lookups
- [**libsodium**](https://libsodium.org/) - End-to-end encryption (X25519, XSalsa20-Poly1305, Ed25519)
- [**libsodium-bcrypt-pbkdf**](https://github.com/imaami/libsodium-bcrypt-pbkdf) - Decrypt password-protected SSH Ed25519 keys
- [**BearSSL**](https://bearssl.org/) - HTTPS client for GitHub/GitLab key fetching; AES for SSH key decryption
- [**zstd**](https://facebook.github.io/zstd/) - Frame compression for bandwidth efficiency
- [**Opus**](https://opus-codec.org/) - Real-time audio codec for low-latency voice transmission
- [**Sokol**](https://github.com/floooh/sokol) - Header-only cross-platform utilities (timing, audio)
- [**SQLite3**](https://www.sqlite.org/) - Database for ACDS discovery service
- [**OpenSSL**](https://www.openssl.org/) - TLS for WebRTC TURN servers and musl static builds
- [**libdatachannel**](https://github.com/paullouisageneau/libdatachannel) - WebRTC DataChannels for P2P NAT traversal
- [**miniupnpc**](https://miniupnp.tuxfamily.org/) - Automatic UPnP port forwarding (optional, fallback to WebRTC)
- [**FFmpeg**](https://ffmpeg.org/) - Media file streaming support

#### Operating System APIs

> 🔌 **Cross-platform details: [Platform Abstraction Layer](https://zfogg.github.io/ascii-chat/group__platform.html#topic_platform)**

ascii-chat uses native platform APIs for each platform for webcam access:

- **Linux**: V4L2 Linux kernel module
- **macOS**: AVFoundation native macOS API
- **Windows**: Media Foundation native Windows API

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
