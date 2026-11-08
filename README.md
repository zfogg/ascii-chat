# 💻📸 ascii-chat 🔡💬 Video chat in your terminal

🌐 **[ascii-chat.com](https://ascii-chat.com)** - Homepage, installation, and documentation

📚 **[API Reference & Man Pages](https://ascii-chat.com/man3)** - Developer API reference and C library documentation

Probably the first command line video chat program (let me know if this isn't
true). Initial commits _November 20-24, 2013_, with
[@craigpalermo](https://github.com/craigpalermo), at some collegiate hackathon.

ascii-chat is an ascii-art communication application that operates over TCP/IP, Websocket, and WebRTC. It
supports color and audio and crypto and compression and multiple clients
and has many little features and options.

The client functions by simply printing text and terminal escape codes to your
terminal, so it works EVERYWHERE that terminals work: on rxvt-unicode in
OpenBox, in a PuTTY SSH session, in iTerm and Kitty.app on macOS, and
theoretically everywhere else terminals run. You just need a webcam.

ascii-chat even works in an initial unix login shell. You know, the shell that
runs 'startx' and launches your desktop environment so you can open a gui
terminal app like Konsole or Kitty or Alacritty in kde or xfce. You don't need a
desktop environment at all to video chat with ascii-chat. (\*)

🆕 We support UTF-8 now so it's not just ASCII anymore. However, the name is still ascii-chat.

🆕 Now 3+ simultaneous people can connect and the server will render the clients to each other as a grid, like Google Hangouts and Zoom calls do. See the **[Network Protocol](https://ascii-chat.com/man3?page=network.c)** in the man3 pages.

🆕 Audio is now supported - turn on your microphone and start talking! See the **[Audio System](https://ascii-chat.com/man3?page=audio.c)** in the man3 pages. (TODO: buggy - needs work). Update: works with --file and --url.

🆕 **Media support with smart resolution!** ascii-chat can turn media from files and URLs into ascii art. Use `--file` for local files (all FFmpeg-supported formats) or `--url` for streaming. For complex streaming sites (YouTube, TikTok, Twitch, etc.), ascii-chat automatically uses yt-dlp to extract playable streams. For direct HTTP streams and media files, FFmpeg handles them natively. You can livestream the result with audio into an ascii-chat call, so you could watch a video with friends and discuss it live, all as ascii art. Pass arbitrary yt-dlp options with `--yt-dlp-options` if needed (e.g., `--yt-dlp-options "--no-warnings"`)

\(\*) Testing needed to verify a decent framerate.

## Animated Demonstration

https://github.com/user-attachments/assets/98ea116a-627a-4f4f-a33e-47854473cdf2

## Code Coverage

[![codecov](https://codecov.io/gh/zfogg/ascii-chat/graph/badge.svg?token=Nkt0GBDMIH)](https://codecov.io/gh/zfogg/ascii-chat)

[![codecov svg](https://codecov.io/gh/zfogg/ascii-chat/graphs/sunburst.svg?token=Nkt0GBDMIH)](https://codecov.io/gh/zfogg/ascii-chat)

## Table of Contents

- [💻📸 ascii-chat 🔡💬 Video chat in your terminal](#-ascii-chat--video-chat-in-your-terminal)
  - [Animated Demonstration](#animated-demonstration)
  - [Code Coverage](#code-coverage)
  - [Table of Contents](#table-of-contents)
  - [Get ascii-chat](#get-ascii-chat)
  - [Build From Source](#build-from-source)
  - [Usage and --flags](#usage-and---flags)
  - [MCP Server](#mcp-server)
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
- Documentation: **[ascii-chat.com/man3](https://ascii-chat.com/man3)** — C library API reference and man3 pages
- Source installation: follow the [Build from source](#build-from-source) steps

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
./scripts/install-deps.sh     # Linux or macOS
# ./scripts/install-deps.ps1  # Windows

# 4. Configure and build with Clang and Ninja
cmake --preset default -B build
cmake --build build

# 5. Install (use sudo if the install prefix requires it)
cmake --install build

# 6. Run ascii-chat
# Learn how to use it
ascii-chat --help
man ascii-chat
# Use ascii-chat
ascii-chat mirror -S -D 0  # Take an ASCII art selfie with your webcam
ascii-chat server          # Start server
ascii-chat client          # Connect client (in another terminal)
```

For detailed build instructions, configuration options, and troubleshooting, see the **[Build System Documentation](https://zfogg.github.io/ascii-chat/group__build.html)**.

See [Dependency builds](docs/dependency-builds.md) for upstream CMake targets,
static and shared linkage, and the source dependency smoke tests.

### Useful CMake Targets

### Regenerating discovery SQL

Discovery and rate-limit queries live in `lib/discovery/sql/` and
`lib/network/rate_limit/sql/`. After editing them, run:

```sh
make generate-sql
make check-sql
```

These targets initialize the pinned mmmbed submodule if needed and build
`bin/mmmbed` (`bin/mmmbed.exe` on Windows) when missing or when the pinned
revision changes. The generated header is `lib/discovery/queries_generated.h`;
commit it with the SQL changes. The binary and intermediate build files are
ignored by Git. Mmmbed uses its pinned Sequel Go dependency to format SQL, so
contributors generate the same output without a separate Sequel executable.

Building mmmbed needs Git, Go 1.26, CGo and a C compiler. Go 1.21 or newer can fetch
the required toolchain automatically; the dependency installers also provision
the version specified by mmmbed for older Go installations. The make targets
use Clang on Unix and MinGW GCC on Windows; `GO` and `SQL_CC` can be overridden
when invoking make.

Common development targets (run with `cmake --build build --target <name>`):

| Target                                                                    | Description                                                                                                                                                   |
| ------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ascii-chat`                                                              | Build the main executable (default)                                                                                                                           |
| `shared-lib`                                                              | Build shared library (`libasciichat.so`/`.dylib`/`.dll`)                                                                                                      |
| `static-lib`                                                              | Build static library (`libasciichat.a`)                                                                                                                       |
| `tests`                                                                   | Build test executables when available (see testing instructions)                                                                                              |
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

ascii-chat uses a unified binary with `server`, `client`, `mirror`, `discovery`, `discovery-service`, and `mcp-server` modes. Discovery is the default when no mode is specified.

**Get help:**

```bash
ascii-chat --help                      # Top-level help
ascii-chat server --help               # Server-specific help
ascii-chat client --help               # Client-specific help
ascii-chat mirror --help               # Mirror-specific help
ascii-chat discovery --help             # Discovery-specific help
ascii-chat discovery-service --help     # Discovery-service-specific help
ascii-chat mcp-server --help            # MCP server help
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

### Zalgo text

Use `--zalgo` to add combining marks to terminal text output. The default
intensity is 7; choose a level from 1 to 10 with, for example, `--zalgo=3`.
This option is available in client, mirror, and discovery modes.

### Audio visualizations

Use `--waveform` to view the selected audio as a live, volume-shaped waveform, or
`--fft` to view a scrolling frequency display. The displays replace the video
image, follow the source selected by `--audio-source` (all by default), and adapt their colors to
dark and light terminal backgrounds. Choose one visualization at a time.

```bash
# Inspect the audio track in a local video
ascii-chat mirror --file music.mp4 --audio-source media --waveform
ascii-chat mirror --file music.mp4 --audio-source media --fft

# Inspect received call audio
ascii-chat client --audio-source call --fft
```

**Client video uploads:**

The CLI defaults to `--video-codec auto`: it tries HEVC (H.265), then H.264, then
uncompressed RGB. If encoding fails during a call, automatic mode advances through
the same order. Explicit `--video-codec hevc` and `--video-codec h264` require that
encoder and report an error if it cannot encode; `--video-codec raw` bypasses
encoding for troubleshooting. `ASCII_CHAT_VIDEO_CODEC` sets the same preference.

**Shell Completions:**

Tab completion is available for Bash, Fish, Zsh, and PowerShell. Completions are auto-generated from the options registry at build time.

**If installed via package manager (Homebrew, AUR, etc.):** Completions are already installed in standard directories. You may need to setup your shell's completion path:

```bash
# Zsh: Add Homebrew/Linuxbrew completions to FPATH (~/.zshrc)
export FPATH=$(brew --prefix)/share/zsh/site-functions:$FPATH

# Bash: if you have a custom BASH_COMPLETION_USER_DIR
export BASH_COMPLETION_USER_DIR=~/.bash-completions
ascii-chat --completions bash > $BASH_COMPLETION_USER_DIR/completions/ascii-chat.bash
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

## MCP Server

Let an MCP client render files, URLs, webcams, and audio visualizations through
`ascii-chat mcp-server`. The local stdio server provides `render_file`,
`render_url`, `render_webcam`, and `render_visualization` (waveform or FFT), using
the same rendering pipeline as the CLI.

```sh
# Local stdio transport: launch this command from your MCP client
ascii-chat mcp-server

# Local Streamable HTTP transport (all five tools)
ascii-chat mcp-server --transport http --address 127.0.0.1 --port 27228
# Add --server for cloud restrictions
ascii-chat mcp-server --transport http --server --address 0.0.0.0 --port 27228
```

Tool arguments accept CLI rendering options such as `color-mode`, `width`,
and `height`. `device_list` lists camera/microphone names, indices, and defaults.
`render_webcam` accepts `video_device`, `audio_device`, and `volume` (0–1 microphone
gain); timed exports include microphone audio unless `options.audio` is false.
`render_visualization` also accepts `audio_device` and `volume` for microphone input.
Use `render-font-size` to control export size. Local `output_path` saves ASCII art as an image
or video without overwriting existing files; set `render-font-size` to 28 for
larger output and `snapshot-delay` to a positive duration for a clip. Files must
be on the server's computer, or supplied as base64 bytes by the MCP client.
The --server flag restricts filesystem/device access and requires deployment isolation
and network egress controls when exposed publicly.

See the **[MCP website guide](https://ascii-chat.com/docs/mcp)** or
[repository documentation](docs/mcp.md) for client configuration, all five tools,
exports, limits, networking, and diagnostics.

## Cryptography

See **[ascii-chat.com/crypto](https://ascii-chat.com/crypto)** for complete cryptography documentation.

## Environment Variables

See **[ascii-chat.com/man1/#ENVIRONMENT](https://www.ascii-chat.com/man1#ENVIRONMENT)** for complete environment variable documentation.

Interactive terminal modes show instructions the first time each mode is used. Press `?` for keyboard controls at any time. Set `ASCII_CHAT_DISABLE_FIRST_USE=1` to skip the first-use notice; set `ASCII_CHAT_FIRST_USE_DIR` to choose where its per-mode markers are stored.

## ascii-chat Internet Protocol (ACIP)

- 📡 **User Network Documentation: [ascii-chat.com/docs/network](https://www.ascii-chat.com/docs/network)**
- 📡 **Developer API Reference: [Man3 Network Protocol Pages](https://ascii-chat.com/man3?page=network.c)**

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

**The ACDS Solution:** Share a three-word session string like `purple-mountain-lake` and people can join. Depending on the network, a direct connection or relay may require additional configuration.

### How ACDS Works

ACDS is a rendezvous service for finding sessions by a memorable session string.
Participants exchange connection information through the service and attempt to
connect using available transports. Connection options depend on the network,
configuration, and whether a relay is available.

### Local network discovery

Servers advertise on the local network using mDNS by default. On the same LAN,
start a server and scan for it from a client:

```bash
ascii-chat server
ascii-chat client --scan
```

The client presents discovered servers for selection. In client mode, `--scan`
is used when no explicit destination has been supplied. LAN discovery does not
require an ACDS server, but does require a network that permits mDNS traffic.

### Session Strings

ACDS generates memorable session identifiers from a curated word list. The format is `adjective-noun-noun` (e.g., `bright-forest-river`), with 16.7M+ possible combinations for collision resistance. They're easy to speak over the phone, remember briefly, and type without errors. Session strings expire when the server disconnects.

### NAT Traversal Technologies

Depending on the connection and configuration, ascii-chat can use UPnP or
NAT-PMP port mapping, or WebRTC ICE with STUN and TURN. Router support, network
policy, and relay availability affect which connections succeed. Use
`--port-forwarding` to request automatic port mapping where supported.

### Network Error Handling

ACDS reports errors such as a missing or full session, invalid password or
signature, rate limiting, and an unavailable session string.

### Privacy & Security

**ACDS never sees your data:**

- Only exchanges connection metadata (IP addresses, ports)
- Media travels between participants using ACIP encryption; a TURN relay may carry traffic when configured
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

"Join happy-sunset-ocean" is easier than "Connect to 73.251.42.118:27224". Connection options depend on network and relay availability. mDNS supports LAN discovery without ACDS. You can run your own ACDS server for privacy.

## libasciichat

ascii-chat is built on a modern, reusable C library called **libasciichat** that can be embedded in other projects. The library provides cross-platform code for developing ascii-chat internet protocol (ACIP) applications. You can install libasciichat as a development dependency and use to build other ACIP applications.

**What's in the library:**

- **Network Protocol**: Full implementation of the ascii-chat internet protocol with encrypted packet exchange, lossless frame compression (zstd), and audio codec integration (Opus). See the protocol reference in the docs.
- **Image Processing**: The `video` module converts images and videos to ASCII art with hardware acceleration via SIMD (AVX2, NEON, SSE) for 1-4x performance gains. Includes grid layout algorithms for multi-client rendering.
- **Platform Abstraction**: Write once, run anywhere. Cross-platform abstractions for threads, mutexes, read-write locks, condition variables, sockets, and terminal I/O that work identically on Windows, macOS, and Linux.
- **Media Support**: Audio capture, mixing, and playback via PortAudio; webcam integration with V4L2 (Linux), AVFoundation (macOS), and Media Foundation (Windows); frame buffering and synchronization. File media support via ffmpeg.
- **Cryptography**: End-to-end encryption with libsodium (X25519 key exchange, XSalsa20-Poly1305 AEAD, Ed25519 signatures) and SSH key authentication with agent support.
- **Debugging & Profiling**: Built-in memory leak detection with source file/line tracking, lock contention analysis, AddressSanitizer integration, and comprehensive logging infrastructure.
- **Memory Management**: Bounded aligned buffers with explicit ownership, reusable worker storage, and packet queues with byte limits and explicit overflow policies.

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

See the online **[API documentation](https://zfogg.github.io/ascii-chat/)** for detailed function references and architecture guides. The complete C library API is documented in the **[man3 pages](https://ascii-chat.com/man3)** — function references, structures, type definitions, and module documentation.

## Open Source

ascii-chat is an open source project with an MIT license, meaning you can and should use our code to make cool stuff. Follow through this Open Source section of the README and you'll be compiling against libasciichat in no time.

> 💡 **For detailed API documentation**, see the **[man3 pages](https://ascii-chat.com/man3)** — includes function references, architecture, and module documentation.

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
- [**libuv**](https://libuv.org/) - Cross-platform asynchronous networking and event-loop support
- [**PortAudio**](http://www.portaudio.com/) - Cross-platform audio I/O
- [**FFTW3**](https://www.fftw.org/) - Fast Fourier transforms used by audio processing
- [**ALSA**](https://www.alsa-project.org/) - Linux audio backend used by PortAudio (Linux only)
- [**tomlc17**](https://github.com/cktan/tomlc17) - ascii-chat has TOML config files
- [**uthash**](https://troydhanson.github.io/uthash/) - ascii-chat uses hash tables normally for a few different things
- [**utf8proc**](https://github.com/JuliaStrings/utf8proc) - UTF-8 support even though it's named ascii-chat
- [**PCRE2**](https://www.pcre.org/) - Regular expressions for strings; validation and parsing and search
- [**ICU**](https://icu.unicode.org/) - Unicode and locale support
- [**libsodium**](https://libsodium.org/) - End-to-end encryption and authentication
- [**libsodium-bcrypt-pbkdf**](https://github.com/imaami/libsodium-bcrypt-pbkdf) - Decrypt password-protected SSH Ed25519 keys
- [**OpenSSL**](https://www.openssl.org/) - TLS and cryptographic support for WebRTC, WebSockets, and TURN credentials
- [**BearSSL**](https://bearssl.org/) - HTTPS client for GitHub/GitLab key fetching; AES for SSH key decryption
- [**zlib**](https://zlib.net/) - Compression support required by libwebsockets and FFmpeg
- [**zstd**](https://facebook.github.io/zstd/) - Network data compression
- [**Opus**](https://opus-codec.org/) - Audio codec for low-latency voice transmission
- [**Abseil**](https://abseil.io/) - C++ support library used by WebRTC audio processing
- [**WebRTC AEC3**](https://github.com/nicnacnic/webrtc_AEC3) - WebRTC acoustic echo cancellation so you can talk to each other without hearing yourself
- [**Sokol**](https://github.com/floooh/sokol) - Header-only cross-platform utilities (timing, audio)
- [**SQLite3**](https://www.sqlite.org/) - Database for discovery service session storage
- [**libdatachannel**](https://github.com/paullouisageneau/libdatachannel) - WebRTC DataChannels
- [**libwebsockets**](https://libwebsockets.org/) - WebSocket server for browser clients
- [**miniupnpc**](https://miniupnp.tuxfamily.org/) - Opens ports in your router to ascii-chat so clients can connect
- [**mDNS**](https://github.com/avahi/avahi) - Local-network service discovery
- [**yyjson**](https://github.com/ibireme/yyjson) - Fast JSON parsing for structured data and logging
- [**FFmpeg**](https://ffmpeg.org/) - Media support for `--file` and `--url`. Direct handling of FFmpeg-supported formats, protocols (HTTP, RTSP, RTMP), and files
- [**x264**](https://www.videolan.org/developers/x264.html) - H.264 encoder used by FFmpeg when enabled
- [**x265**](https://www.videolan.org/developers/x265.html) - HEVC encoder used by FFmpeg when enabled
- [**yt-dlp**](https://github.com/yt-dlp/yt-dlp) - Supports 1000+ streaming sites for `--url` (YouTube, TikTok, Twitch, etc.). Optional; ascii-chat falls back to FFmpeg for direct streams (optional)
- [**libvterm**](http://www.leonerd.org.uk/code/libvterm/) - Terminal emulation for `--render-file` pixel output
- [**FreeType2**](https://freetype.org/) - Font rasterization for `--render-file` glyph rendering
- [**fontconfig**](https://www.freedesktop.org/wiki/Software/fontconfig/) - Font resolution and configuration for `--render-file`
- [**libexecinfo**](https://github.com/termux/libexecinfo) - Backtrace support for musl builds (musl only)
- [**Criterion**](https://criterion.readthedocs.io/en/master/) - C test framework (Unix test builds only)
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

The first CMake configuration installs vcpkg manifest dependencies. Later
configurations reuse them without waiting for another vcpkg install. After
changing the manifest or vcpkg configuration, refresh the dependencies with
`cmake --preset default -B build -DASCIICHAT_VCPKG_REFRESH=ON`, then set the
option back to `OFF` for subsequent configurations. If
`VCPKG_MANIFEST_INSTALL` was explicitly disabled, set it to `ON` for the refresh.

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

The documentation is automatically generated from source code using Doxygen and published as man3 pages on **[ascii-chat.com/man3](https://ascii-chat.com/man3)**. The original Doxygen HTML is available on **[GitHub Pages](https://zfogg.github.io/ascii-chat/)** if you prefer that format.

### Testing

> 🧪 **Full documentation: [Testing Framework](https://ascii-chat.com/man3?page=tests_common.c)** in the man3 pages

#### Testing Framework

- **Framework**: [libcriterion](https://criterion.readthedocs.io/en/master/)
- **Coverage**: CI publishes one Codecov upload after all seven Ubuntu and macOS unit, integration, and performance reports are present, with completed CTest results and nonzero Criterion cases. Download the `complete-coverage` artifact for the original reports and a summary of executed-line coverage. Completed runs contribute coverage even when assertions fail; those failures remain visible in the test checks. The badge uses Codecov's coverage metric, which counts partially covered branches as uncovered lines; it can be lower than gcovr's executed-line percentage.
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
# Enable tests in the default build directory
cmake --preset default -B build -DBUILD_TESTS=ON

# Build tests (where the tests target is available)
cmake --build build --target tests

# Run all tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific test categories using labels
ctest --test-dir build --label-regex "^unit$" --output-on-failure
ctest --test-dir build --label-regex "^integration$" --output-on-failure
ctest --test-dir build --label-regex "^performance$" --output-on-failure

# Run specific tests by name pattern
ctest --test-dir build -R "buffer-ownership" --output-on-failure

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
# Configure with tests enabled and build the desired test targets
cmake --preset default -B build -DBUILD_TESTS=ON
cmake --build build --target test_unit_mixer test_performance_ascii_simd

# Run individual tests
build/bin/test_unit_mixer --verbose
build/bin/test_performance_ascii_simd --filter "*monochrome*"
```

## TODO

- [x] Audio.
- [x] Client should continuously attempt to reconnect
- [x] switch Client "-a/--address" option to "host" and make it accept domains as well as ipv4
- [x] Colorize ASCII output
- [/] 60fps in Kitty and the browser.
- [ ] Refactor image processing algorithms
- [ ] Edge detection and other things like that to make the image nicer.
- [ ] Square packing / grid packing / putting multiple squares into one square efficiently. For the multi-client grid layout system.
- [x] Grid packing algorithm.
- [x] client reconnect logic
- [x] terminal resize events
- [x] A nice protocol for the thing (packets and headers).
- [x] client requests a frame size
- [x] Client should gracefully handle `frame width > term width`
- [x] Client should gracefully handle `term resize` event
- [x] Compile to WASM/WASI and run in the browser
- [x] Socket multiplexing.
- [x] WebRTC transport.
- [x] Websocket transport.
- [x] JavaScript Websocket transport.
- [x] Multiple clients. Grid to display them.
- [x] Snapshot mode for clients with --snapshot to "take a photo" of a call and print it to the terminal or a file, rather than rendering video for a long time.
- [x] Audio mixing for multiple clients with compression and ducking.
- [x] WebRTC AEC3.
- [x] Color filters so you can pick a color for all the ascii so it can look like the matrix when you pick green (Gurpreet suggested).
- [x] Lock-free packet send queues.
- [x] Hardware-accelerated ASCII-conversion via SIMD.
- [x] Windows support.
- [x] Linux support.
- [x] Fix v4l2 usage because the ascii rendered looks corrupted on Linux.
- [x] Crypto. Custom handshake protocol with auth and rekeying.
- [x] GPG key support for crypto (Ed25519 via gpg-agent).
- [x] Matrix digital rain (--matrix works. try with --color-filter rainbow).
- [x] Zalgo text generator (`--zalgo`).
- [x] v4l2 webcam images working.
- [ ] make more little todos from the github issues so they're tracked in the readme because i like the changelog (i can check with git when things are checked off)
- [ ] submit an ascii-chat internet protocol (ACIP) ietf rfc
- [/] ascii-chat discovery service
- [ ] submit an ascii-chat discovery service (ACDS) ietf rfc
- [x] ascii-chat homebrew tap (zfogg/homebrew-ascii-chat)
- [ ] ascii-chat official homebrew bottles (brew install ascii-chat with no tap)
- [ ] get libgcrypt and gpg fixed and integrate with libgcrypt

## Links

- [Everything you never wanted to know about ANSI escape codes](https://notes.burke.libbey.me/ansi-escape-codes)

## Notes

- **Note:** Colored frames are many times larger than monochrome frames due
  to the ANSI color codes.

- We don't really save bandwidth by sending color ascii video. I did the math with Claude Code.
