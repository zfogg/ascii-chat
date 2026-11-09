# ascii-chat Development Guide for Claude

> **Current repository guidance (updated 2026-11-08)**
>
> This guide retains the detailed subsystem notes below, but the repository has
> changed substantially since the original version was written. The current
> source of truth for a workflow is the checked-in CMake preset, workflow,
> topic page, or skill named in the relevant section. If this guide conflicts
> with those files, inspect the current implementation and update this guide
> as part of the same change.

## Current State and Recent Direction

The recent development cycle has broadened ascii-chat from a TCP/WebSocket-
centric terminal application into a shared native/browser transport stack.
The major changes visible in the last six months are:

- The ACIP transport layer now supports native QUIC and browser WebTransport
  in addition to TCP, WebSocket, and WebRTC paths. Native QUIC and HTTP/3
  WebTransport share the server's UDP endpoint and the same ACIP session
  machinery; do not implement a second protocol path for browser behavior.
- QUIC transport security is layered. TLS 1.3 authenticates the transport
  endpoint, while ACIP authentication protects the application session and
  media metadata. Stream traffic and negotiated DATAGRAM traffic have separate
  readiness, routing, and failure handling. Transport readiness alone is not
  enough to send media.
- Negotiated QUIC DATAGRAM support carries audio and visual media when the peer
  permits it. Datagram loss is expected: use the bounded reassembly and video
  recovery paths rather than assuming ordered delivery. Reliable ACIP streams
  remain the fallback for peers without usable datagrams.
- Discovery can advertise native QUIC and browser WebTransport endpoints,
  preserve the authoritative discovery host during handoff, and rotate
  authenticated application-key epochs while media is live. Discovery and
  direct transport code must use the shared authenticated-record and rekey
  abstractions.
- The wire compatibility marker is currently protocol `2.3.0`. Change the
  protocol constants deliberately and update the compatibility tests and
  release annotation when changing packet layouts, negotiation, or encoded
  media contracts.
- The MCP server is a first-class mode and can render media, serve temporary
  hosted downloads, and expose browser/native-compatible output formats. Keep
  MCP cancellation, bind validation, credential-bearing URL redaction, and
  output cleanup on the existing ownership paths.
- Native rendering now includes selectable renderers, temporal stabilization,
  motion-aware per-cell history, improved glyph and braille coverage, and
  browser/native parity work. Rendering hot paths must remain allocation-free,
  deterministic under tests, and independent of logging or blocking I/O.
- Python test support has migrated to `uv` and pytest. Native Python drivers,
  extractor tests, WebRTC smoke coverage, and many integration tests now use
  the project test environment rather than a manually managed virtualenv.
- SQL discovery queries are embedded from the pinned Sequel/mmmbed toolchain.
  Generated catalogs must remain consistent across platforms, and the native
  mmmbed binaries are selected for the host, including Windows ARM64.
- Documentation has a standalone CMake/Doxygen project, curated topic
  categories, and a Pages deployment workflow. Documentation is welcome and
  encouraged, but do not create random Markdown files under `docs/`: files
  that are not part of the rendered Doxygen navigation are easy to miss and
  never appear in the browser. Put durable architecture, subsystem, workflow,
  and task documentation in `docs/topics/*.dox`, and update the curated Topic
  Categories page at `docs/topics/index.dox` when a topic belongs in its
  navigation. Modify existing rendered documentation freely when it improves
  the site; keep every new page connected to the Doxygen topic structure.

## Repository Rules That Are Easy to Miss

### Preserve the working tree

Before changing anything, run `git status --short --branch`. Existing edits
belong to the user unless they are clearly part of the requested task. Stage
named files only; never use `git add .` or `git add -A` while unrelated work is
present. Do not remove `build/`, dependency caches, generated browser output,
or another task's worktree just to obtain a clean build.

### Use the repository's skills and current authorities

The repository's `.agents/skills/` guidance is part of the development
contract. For native C work, read `ascii-chat-c-development/SKILL.md`; for
tests, read `ascii-chat-testing/SKILL.md`; for Python tooling, read
`ascii-chat-python-development/SKILL.md`; and for cross-platform builds, read
`ascii-chat-cross-platform-development-and-testing/SKILL.md`. For public API,
transport, subsystem, option, or workflow changes, read
`ascii-chat-documentation/SKILL.md` and update the relevant docs in the same
change.

When the task is a release, use the CLI release skill. CLI tags (`vX.Y.Z`) and
library tags (`lib/vX.Y.Z`) are independent. Release tags are annotated and
immutable: never move, delete, force-push, or reuse a tag after publishing it.

### Prefer abstractions over new parallel paths

Network features belong in the shared transport, ACIP, negotiation, packet,
crypto, and media pipelines. Native QUIC, WebTransport, WebRTC, WebSocket,
and TCP adapters must satisfy the same transport contract. Do not add a
browser-only packet format, a second media queue, or a mode-specific crypto
handshake when the shared layer already owns that concern.

Public structures and packet fields have three documentation surfaces: the
header/Doxygen comments, the curated topic pages, and the data-structure
reference. Keep names, ownership, byte widths, optionality, and lifecycle
claims consistent across all three. The generated data-structure summaries
describe ownership; they are not permission to expose private implementation
details as public API.

## Current Build and Test Workflow

The native build uses Clang, Ninja, and the checked-in CMake presets. On
Windows use the native PowerShell environment and the `default` preset; do not
substitute MSVC or GCC. Reconfigure an existing build rather than deleting it.

```bash
# Standard sanitized debug build
cmake --preset default -B build
cmake --build build

# Faster debug build without sanitizer instrumentation
cmake --preset debug-fast
cmake --build build-fast-debug

# Configure, build, and run the complete native test preset
cmake --preset tests
cmake --build --preset tests
ctest --preset all

# Narrow test groups
ctest --preset unit
ctest --preset integration
ctest --preset performance
```

The preset names and binary directories in `CMakePresets.json` are authoritative.
The `default` preset intentionally has `BUILD_TESTS=OFF`; use `tests` or
`github` when tests are required. `release`, `release-musl`, `static`,
`shared`, `windows-release`, `docker`, `coverage`, `panic`, and the web/iOS
configurations have different dependency and instrumentation contracts; do
not infer one preset's behavior from another.

Python commands must use the locked `uv` environment:

```bash
uv sync --project tests/ascii_chat_tests --frozen
uv run --project tests/ascii_chat_tests --frozen pytest tests/python/...
uv run --project tests/ascii_chat_tests --frozen python tests/standalone/...
```

Set `Python3_EXECUTABLE` to the interpreter returned by the test environment
when configuring CMake if CMake or CTest will invoke Python. Do not use
`python -m pip`, a system Python, or a hand-created virtualenv. Change Python
dependencies in the appropriate `pyproject.toml` and lockfile.

For migrated tests, prefer pytest selection and fixtures over invoking old
standalone drivers directly. Use the current test layout under
`tests/python/`, `tests/standalone/`, and `tests/scripts/`; inspect the test's
README or `pyproject.toml` when a command is unclear. Python lint/type checks
use the locked tooling and are part of the CI contract.

### Documentation and formatting checks

The documentation project can be verified without configuring the native
application:

```bash
cmake -S docs -B build_docs -DASCII_CHAT_SOURCE_DIR="$PWD"
cmake --build build_docs --target docs
```

The formatter workflow has a dependency-free standalone configuration. Changed
C/C++ files are checked with clang-format in CI, and pushes to master may
produce a metadata-only formatting commit. Avoid editing generated HTML or
browser artifacts by hand; regenerate them through the documented build.

## Transport and Protocol Notes

Read `docs/topics/network.dox`, `docs/topics/quic.dox`, `docs/topics/handshake.dox`,
`docs/topics/client_protocol.dox` before
changing connection setup. Read `docs/acip-preparation.md` before changing
wire formats. The protocol version constants live in
`include/ascii-chat/common/protocol_constants.h`.

The connection lifecycle is intentionally staged:

1. The adapter establishes transport connectivity and reports transport state.
2. QUIC/WebTransport waits for TLS, endpoint trust, and application security
   readiness; these are distinct from a socket being connected.
3. ACIP negotiation exchanges protocol/version and capability information.
4. Reliable control and media streams are routed through the shared packet
   scheduler; optional datagrams are enabled only after capability negotiation.
5. Discovery, handoff, and key rotation update shared authenticated records,
   then media resumes through the normal readiness gates.

When adding or changing a packet:

- update packet type registration, length validation, serialization, parsing,
  logging names, and all transport adapters;
- preserve packed wire widths independently of application storage widths;
- validate received counts and lengths before indexing, allocating, or
  serializing;
- cover native, browser/WASM, and fallback transports where the packet is
  supported;
- update protocol topics and tests before changing the compatibility marker.

Do not silently fall back from an explicitly requested `quic://` endpoint to a
different transport. Endpoint URL selection is part of the current browser and
native transport contract. WebTransport uses an advertised HTTPS origin and
HTTP/3, while native QUIC uses the explicit QUIC listener. Both still carry
ACIP traffic and must preserve authentication and cancellation semantics.

## Current Media, Audio, and Rendering Notes

Audio capture, preview, mixing, encoding, transport delivery, and playback are
separate ownership domains. The audio callback must not perform telemetry,
blocking I/O, allocation, or logging. Device changes keep capture alive where
possible and use the existing fallback/error-scope behavior. Audio batches may
use negotiated datagrams but must retain a reliable path for peers without
that capability.

Video frames carry acquisition and telemetry metadata through the shared frame
pipeline. Render selection, palette, glyph support, stabilization history, and
browser/native output are negotiated or configured through existing options;
do not hard-code a renderer in a transport or mode. Temporal stabilization and
motion-aware cell history are stateful: reset them on media, dimension, or
source changes and test both fresh-frame and transition behavior.

For media URLs, preserve the direct FFmpeg path, yt-dlp extraction path,
credential redaction, cancellation, and audio-first stream handling. Use fake
yt-dlp fixtures for tests that should not contact a real service. Keep cached
stream metadata scoped by URL and extraction options, and do not log secrets
embedded in URLs or headers.

## Current Tooling and Deployment Notes

The MCP server has native and hosted workflows. Validate bind addresses and
download handles, keep temporary artifacts bounded and cancellable, and make
HTTP status/error handling use the shared parser. MCP rendering should reuse
the same media and renderer pipeline as the native application. Hosted download
routes are temporary and must not be treated as permanent public storage.

The query and panic tools are development tools, not production dependencies.
The query tool uses the host's LLDB/LLVM facilities and the panic tool builds
instrumented source-print variants. Their build files must remain isolated from
the normal application target and must not silently add required runtime
dependencies to the main binary.

SQL generation is pinned and reproducible. Use the repository's embedded
mmmbed/Sequel toolchain and host-specific binaries; do not regenerate catalogs
with an arbitrary Go installation. Keep generated SQL stable under formatting,
use named bindings, and update all platform catalogs together.

## Six-Month History Review Checklist

The recent history added many follow-up fixes to large feature branches. When
reviewing or extending one of those systems, inspect the whole feature family,
not only the first `feat:` commit. In particular, search history for:

- `QUIC`, `WebTransport`, `ACIP`, `rekey`, `datagram`, `discovery`, and
  `handshake` before modifying network behavior;
- `pytest`, `uv`, `extractor`, `WebRTC`, and `sanitizer` before changing tests;
- `renderer`, `stabilization`, `glyph`, `braille`, and `terminal` before
  changing display behavior;
- `MCP`, `download`, `bind`, `telemetry`, and `cancellation` before changing
  the MCP server;
- `mmmbed`, `Sequel`, `SQL catalog`, and `Windows ARM64` before changing
  discovery query generation;
- `Doxygen`, `Pages`, `formatter`, and `standalone` before changing CI or docs.

Use `git log --since='6 months ago' -- <path>` and inspect the relevant diffs,
including follow-up fixes and tests. Commit messages are useful orientation,
not proof of behavior; verify the current source and tests before documenting
an assertion.

## Repository Information

- **Repository Owner**: zfogg (GitHub username)
- **Repository**: ascii-chat
- **Description**: Terminal-based video chat with ASCII art rendering, end-to-end encryption, and multi-client support

## Quick Start

### Building

```bash
# Configure with Ninja (default preset)
cmake --preset default -B build

# Build
cmake --build build

# Run tests
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0
```

**IMPORTANT**:

- This project uses Clang only, not MSVC or GCC
- Use `cmake --preset default` - use Ninja and don't switch to Makefiles or Visual Studio generators
- **Avoid deleting build/** - just re-run `cmake --preset default` and rebuild

### Running

```bash
# Server mode (listens on port 27224)
./build/bin/ascii-chat server

# Client mode (connects to localhost:27224)
./build/bin/ascii-chat client

# Mirror mode (local webcam preview, no networking)
./build/bin/ascii-chat mirror

# Discovery service (ACDS)
./build/bin/acds --port 27225
```

Get full options with: `./build/bin/ascii-chat <mode> --help`

## Help System & Debug Options

### Complete current flag inventory

The lists below were refreshed from the current generated `--help` output on
2026-11-08. They are intentionally broader than the abbreviated descriptions
later in this file. Always confirm mode availability and defaults with
`ascii-chat [mode] --help`; binary-level options can be placed before the mode.
Every value-taking option also maps to `ASCII_CHAT_<UPPERCASE_NAME>` with
hyphens changed to underscores.

Common and logging flags:

- `-h`, `--help`, `-v`, `--version`, `--splash-screen`
- `--keepawake`, `--no-keepawake`, `--check-update`, `--no-check-update`
- `-L`, `--log-file FILE`, `--log-level LEVEL`, `-V`, `--verbose [VALUE]`,
  `-q`, `--quiet`, `--grep PATTERN`
- `--color-scheme NAME`, `--json`, `--log-template TEMPLATE`,
  `--log-format-console`
- `--otel-log-level LEVEL`, `--otel-endpoint URL`, `--otel-ca-file FILE`,
  `--otel-enabled-telemetry LIST`, `--otel-realtime-sample-rate VALUE`,
  `--otel-realtime-max-per-second INTEGER`, `--otel-metrics-interval MS`,
  `--otel-timeout MS`, `--otel-queue-capacity INTEGER`

Terminal, configuration, and display flags:

- `-x`, `--width INTEGER`, `-y`, `--height INTEGER`, `--color-mode VALUE`,
  `--show-capabilities`, `--utf8 BOOLEAN`, `--strip-ansi`, `--color BOOLEAN`
- `--config FILE`, `--config-create [FILE]`, `--completions SHELL [FILE]`,
  `--man-page-create [FILE]`
- `--zalgo [LEVEL]`, `--waveform`, `--fft`, `--color-filter VALUE`,
  `-M`, `--render-mode VALUE`, `-P`, `--palette VALUE`, `-C`,
  `--palette-chars STRING`, `--renderer luminance|glyph`, `--stabilize`,
  `--stretch`, `--fps INTEGER`, `--fps-counter`, `-S`, `--snapshot`,
  `-D`, `--snapshot-delay SECONDS`, `--matrix`, `--face-follow`,
  `--face-follow-debug`, `--flip-x`, `--flip-y`

Webcam and audio flags:

- `-c`, `--webcam-index INTEGER`, `--test-pattern [0|1]`, `--list-webcams`
- `--test-audio`, `--audio-spectral-agc`, `--audio-pitch-correct`,
  `--spectral-audio`, `--audio-spectral-gate`, `--audio-multiband`,
  `--audio-adaptive-eq`, `--audio-fft-size INTEGER`, `-A`, `--audio`,
  `--microphone-channels INTEGER`, `--microphone-channel INTEGER`,
  `--microphone-index INTEGER`, `--speakers-index INTEGER`,
  `--microphone-volume VALUE`, `--ivolume VALUE`,
  `--speakers-volume VALUE`, `--volume VALUE`, `--audio-source VALUE`,
  `--audio-capture-source VALUE`, `--audio-analysis`,
  `--no-audio-playback`, `--encode-audio`, `--no-encode-audio`,
  `--list-microphones`, `--list-speakers`

Media and output flags:

- `-f`, `--file FILE`, `-u`, `--url URL`, `-l`, `--loop`, `--pause`,
  `-s`, `--seek TIMESTAMP`, `--yt-dlp-options STRING`
- `--render-kitty`, `--render-file PATH`, `--render-theme THEME`,
  `--render-font FONT`, `--render-font-size SIZE`

Network and discovery flags:

- `-p`, `--port PORT`, `--reconnect-attempts INTEGER`, `--port-forwarding`,
  `--scan`, `--compression-level INTEGER`, `--no-compress`
- `--discovery-service HOST`, `--discovery-service-port PORT`,
  `--discovery-service-url URL`, `--discovery-expose-ip`, `--discovery`,
  `--webrtc-bind-address ADDRESS`, `--webrtc`, `--no-webrtc`,
  `--prefer-webrtc`, `--webrtc-relay-only`, `--webrtc-skip-stun`,
  `--webrtc-disable-turn`, `--webrtc-skip-host`,
  `--webrtc-ice-timeout MS`, `--webrtc-reconnect-attempts INTEGER`,
  `--stun-servers LIST`, `--turn-servers LIST`, `--turn-username NAME`,
  `--turn-credential VALUE`, `--turn-secret VALUE`
- Server-only listener flags include `--websocket-port PORT`,
  `--websocket-tls-cert FILE`, `--websocket-tls-key FILE`,
  `--websocket-tls-key-password VALUE`, `--max-clients INTEGER`,
  `--status-screen`, `--stats-interval SECONDS`, and `--no-audio-mixer`.
- MCP server flags include `--server`, `--transport http|stdio`,
  `--http-origin ORIGIN`, `--address ADDRESS`, `--address6 ADDRESS`,
  `--http-tls-cert FILE`, `--http-tls-key FILE`,
  `--http-tls-key-password VALUE`, and `--max-clients INTEGER`.

Security and diagnostics flags:

- `-E`, `--encrypt`, `-K`, `--key KEY`, `--password PASSWORD`,
  `--no-encrypt`, `--no-auth`, `--server-key KEY`, `--client-keys LIST`,
  `--discovery-insecure`, `--discovery-service-key KEY`
- `--sync-state TIME`, `--errno-stacks TIME`, `--backtrace TIME`,
  `--memory-report TIME`

Several flags are aliases or paired booleans. In particular, `--ivolume` is
an alias for `--microphone-volume`, `--volume` aliases
`--speakers-volume`, and `--no-*` forms explicitly disable defaults that are
otherwise enabled. Options that accept an optional value, such as
`--config-create`, `--man-page-create`, `--test-pattern`, and `--zalgo`, must
be parsed according to the generated help rather than assumed to require a
following token.

**Mode Legend:** S=server, C=client, M=mirror, DS=discovery-service, D=discovery

### Mode Help Commands

- **`./build/bin/ascii-chat --help`** (all) - Shows all global flags, modes, examples, environment variables, and keybindings.
- **`./build/bin/ascii-chat <mode> --help`** (S, C, M, DS) - Mode-specific help with available options for that mode. Key help key `?` in interactive mode shows help overlay.

### Connection & Addressing Options

- **`--address`** (S, C, DS) - Bind address for server (supports IPv4/IPv6, syntax 0-2); client connects to this address with `[host][:port]` format (defaults localhost:27224).
- **`--port`** (S, C, DS) - Port number (default: 27224 for server/client, 27225 for discovery-service).
- **`--discovery-service`** (S, C, D) - Enable discovery service integration for P2P session discovery.
- **`--discovery-service-port`** (S, C, D) - Discovery service server port (default 27225).
- **`--max-clients`** (S) - Maximum concurrent clients (server mode only).

### Media Input Options

- **`--file FILE`** (S, C, M) - Use local file instead of webcam (supports .mp4, .mkv, .webm, etc.; use `-f '-'` for stdin piping).
- **`--url URL`** (S, C, M) - Stream from HTTP/HTTPS URL, HLS (.m3u8), RTSP, or YouTube/TikTok (via yt-dlp).
- **`--yt-dlp-options OPTIONS`** (S, C, M) - Custom yt-dlp flags for streaming site extraction.

### Cryptography & Authentication

- **`--password`** (S, C) - Password-based authentication (ephemeral DH without identity verification).
- **`--key FILE`** (S, C) - SSH key (ed25519 .pem format) for public key authentication; server advertises its key to clients.
- **`--server-key FILE|github:USERNAME|URL`** (C) - Validate server's public key from local file, fetch from GitHub (github:USERNAME), or HTTP(S) URL (TOFU with known_hosts).
- **`--client-keys FILE|github:USERNAME|URL`** (S) - Validate client public keys from local file, GitHub (github:USERNAME), or HTTP(S) URL (server mode).

### Display & Rendering Options

- **`--width`** (M, C, S) - Override terminal width detection.
- **`--height`** (M, C, S) - Override terminal height detection.
- **`--color-mode {auto|none|16|256|truecolor}`** (all) - Force color output mode; CLAUDECODE auto-detects unless piped to file.
- **`--strip-ansi`** (all) - Remove ANSI escape codes (useful for piping to non-terminal or text files).
- **`--show-capabilities`** (all) - Detect and display terminal capabilities (color level, UTF-8 support), then exit immediately.
- **`--splash-screen`** (C, M, D) - Enable or disable splash screen with ASCII art. Use `--splash-screen=false` to disable (overrides CLAUDECODE environment variable).
- **`--status-screen`** (S, DS) - Enable or disable live status screen display. Use `--status-screen=false` to disable (overrides non-interactive terminal checks).

### Snapshot & Recording

- **`--snapshot`** (C, M) - Capture exactly one frame and exit (useful for scripting). **Gotcha**: Doesn't auto-detect terminal dimensions when piped.
- **`--snapshot-delay`** (C, M) - Delay before snapshot (default 0). **Script tip**: Use `--snapshot --snapshot-delay 0 | pbcopy` to copy frame to clipboard on macOS.

### Logging & Debugging Options

All modes support these (debug builds only):

- **`--log-level {DEV|DEBUG|INFO|WARN|ERROR|FATAL}`** (all) - Verbosity control; pair with `--grep` for filtering specific logs.
- **`--log-file PATH`** (all) - Write logs to file instead of stdout/stderr (useful for post-mortem debugging with `--grep` filtering).
- **`--grep /PATTERN/FLAGS`** (all) - Filter logs by regex pattern; flags: `i` (case-insensitive), `F` (literal string), `C#` (context lines), `g` (highlight matches), `I` (invert/exclude).
- **`--sync-state [TIME]`** (all) - Print sync primitives state (mutex/rwlock/condition variables) at optional time interval.
- **`--backtrace [TIME]`** (all) - Print call stack with optional time offset.
- **`--memory-report [TIME]`** (all) - Periodic memory usage report; red numbers indicate leaks.

### Environment Variables

All `--flag-name` options map to `ASCII_CHAT_FLAG_NAME` (hyphens → underscores); precedence: config file < env vars < CLI flags.

- **`CLAUDECODE=1`** - Auto-set by Claude Code; enables splash screen, auto-detects color/UTF-8, disables status screen (override with `--splash-screen` or `--status-screen`).
- **`ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y;n;password123'`** - Stack-based auto-answers for interactive prompts (semicolon-separated; useful for automation/testing).

## Binary Modes

ascii-chat binary has four primary modes:

1. **server** - Video broadcast server, handles multiple clients, mixes audio, generates ASCII grid
2. **client** - Connects to server, sends webcam frames, receives mixed ASCII output
3. **mirror** - Local mode for testing webcam/files/ulrs/encoding/format without networking or crypto
4. **discovery-service** - Session signalling and discovery webrtc server
5. **discovery** - The mode that connects via session string via a discovery-service instance. ascii-chat binary runs as
   this mode by default when you don't pass a mode as a first positional argument.

## Debugging Flags

### Snapshot Mode (Single Frame Capture)

```bash
# Capture exactly one frame and exit
./build/bin/ascii-chat client --snapshot --snapshot-delay 0
# Same thing
./build/bin/ascii-chat client --snapshot -S -D 0

# Capture for N seconds then exit (useful for testing), printing every frame to stdout
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 5

# Common pattern: quick test without continuous capture
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 --volume 0
```

### Log Filtering with --grep

Searches logs and log headers. Supports two formats:

- **Format 1**: `/pattern/flags` - Regex with flags (case-insensitive, context lines, etc.)
- **Format 2**: `pattern` - Plain regex without slashes or flags

```bash
# Plain regex format (no slashes, no flags)
./build/bin/ascii-chat server --grep "DEBUG"
./build/bin/ascii-chat server --grep "handshake|crypto"
./build/bin/ascii-chat client --grep "ERROR|WARN"

# Slash format with flags
./build/bin/ascii-chat server --grep "/handshake/i"        # Case-insensitive
./build/bin/ascii-chat client --grep "/ERROR/C2"           # 2 lines of context
./build/bin/ascii-chat client --grep "/(YO)/F"             # Fixed string (literal)

# Flags: i(case-insensitive), F(fixed string), C/A/B(context), g(global highlight), I(invert)
./build/bin/ascii-chat client --grep "/test/igC2"
./build/bin/ascii-chat client --grep "/test/igB1A3"

# Pass --grep multiple times to OR-logic multiple patterns (can mix formats)
./build/bin/ascii-chat client --grep "/test1/igB1A3" --grep "test2"

# Combine with log levels
./build/bin/ascii-chat --log-level debug server --grep "crypto"
```

### Other Debug Options

```bash
# Log to file instead of stdout+stderr
./build/bin/ascii-chat --log-file /tmp/server.log server

# Increase verbosity
./build/bin/ascii-chat -VV server  # -V, -VV, -VVV

# Set log level explicitly
./build/bin/ascii-chat --log-level dev server
```

## Error Handling with asciichat_errno_t

**ALWAYS use asciichat_error_t for return types, not int:**

```c
// ✅ CORRECT
asciichat_error_t process_data(void) {
    if (some_error) {
        return SET_ERRNO(ERROR_INVALID_STATE, "Failed to process: %s", reason);
    }
    return ASCIICHAT_OK;
}

// ❌ WRONG
int process_data(void) {
    if (some_error) {
        log_error("Failed");
        return -1;  // No context, no error tracking!
    }
    return 0;
}
```

**Error checking:**

```c
asciichat_error_t result = do_something();
if (result != ASCIICHAT_OK) {
    asciichat_error_context_t ctx;
    if (HAS_ERRNO(&ctx)) {
        log_error("Operation failed: %s", ctx.context_message);
    }
    return result;
}
```

## Memory Management

**Use memory tracking macros, not raw malloc/free:**

```c
// ✅ CORRECT
uint8_t *buffer = SAFE_MALLOC(1024, uint8_t *);
client_t *client = SAFE_CALLOC(1, sizeof(client_t), client_t *);
SAFE_FREE(buffer);

// ❌ WRONG
uint8_t *buffer = malloc(1024);  // No leak tracking!
free(buffer);
```

Memory reports show at program exit in debug builds. Red numbers indicate leaks.

## Platform Abstraction Layer (lib/platform)

Cross-platform support via abstraction:

- **lib/platform/posix/** - Linux/macOS implementations
- **lib/platform/windows/** - Windows implementations
- **lib/platform/abstraction.h** - Platform-independent API

Key abstractions:

- Threads: `asciichat_thread_t`, `acthread_create()`
- Mutexes: `mutex_t`, `mutex_init/lock/unlock()`
- Sockets: `socket_t`, `socket_close()`, `INVALID_SOCKET_VALUE`
- Safe wrappers: `SAFE_GETENV()`, `SAFE_STRERROR()`, `platform_open()`

Always use abstraction layer functions, never direct POSIX/Windows APIs.

## Session Management (lib/session)

The session layer handles:

- **display.c** - Terminal capability detection, ASCII rendering setup
- **splash.c** - Intro splash screen with rainbow animation and log capture
- **help_screen.c** - Interactive help UI
- **server_status.c** - Server status screen with live log feed
- **discovery_status.c** - Discovery UI
- **session_log_buffer.c** - Shared log buffer for session screens

### Session Screens (Status + Splash)

Both status screen and splash screen use the same "fixed header + scrolling logs" pattern:

- Fixed header at top (status info or animated ASCII art)
- Logs captured in `session_log_buffer` (100-entry circular buffer)
- Logs displayed below header, calculated to fill exactly `term_rows - header_lines - 1`
- Screen never scrolls past bottom (prevents terminal flashing)

**Implementation:**

- `session_log_buffer_init()` - Initialize log capture (called by splash/status init)
- `session_log_buffer_append()` - Called from `lib/log/logging.c` to capture messages
- `session_log_buffer_get_recent()` - Retrieve N most recent entries for display
- `session_log_buffer_clear()` - Clear initialization logs before screen starts

**Testing:**

```bash
# Manual test script
./tests/manual/test_session_screens.sh

# Server status screen
./build/bin/ascii-chat server --status-screen

# Client splash screen (shows during connection)
./build/bin/ascii-chat client
```

## Media Resolution and Streaming

ascii-chat supports media from files and URLs using a smart resolution system:

### Supported Media Sources

**Direct Streams & Files (FFmpeg-handled):**

- Local files: `.mp4`, `.mkv`, `.webm`, `.avi`, `.mov`, `.flv`, `.gif`, `.jpg`, `.png`, etc.
- Streaming protocols: `http://`, `https://`, `rtsp://`, `rtmp://`, HLS (`.m3u8`), DASH
- Any format or protocol supported by FFmpeg

**Complex Streaming Sites (yt-dlp-resolved):**

- 1000+ supported sites including YouTube, TikTok, Twitch, Reddit, Instagram, Twitter, etc.
- Requires `yt-dlp` installed: `pip install yt-dlp` or `brew install yt-dlp`
- yt-dlp extracts the actual playable stream URL, which is then handled by FFmpeg

### Smart Resolution Strategy

The media source resolver (`lib/media/source.c`) uses this strategy:

1. **Direct detection**: Check if URL has a direct file extension (`.mp4`, `.mkv`, `.m3u8`, etc.) or protocol (`rtsp://`, `rtmp://`) → use FFmpeg directly (fastest)
2. **yt-dlp extraction**: If not a direct stream, try to extract stream URL using yt-dlp (for YouTube, TikTok, etc.)
3. **Fallback**: If yt-dlp fails, try FFmpeg as a last resort (may fail for truly unsupported sites)
4. **Error reporting**: Log failures at each step with the `--grep` system

### Usage Examples

```bash
# Direct file (FFmpeg handles it)
./build/bin/ascii-chat mirror --file /path/to/video.mp4

# Direct HTTP stream (FFmpeg handles it)
./build/bin/ascii-chat mirror --url "https://example.com/stream.m3u8"

# YouTube video (yt-dlp extracts stream, FFmpeg plays it)
./build/bin/ascii-chat mirror --url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"

# TikTok video (yt-dlp extracts stream)
./build/bin/ascii-chat mirror --url "https://www.tiktok.com/@user/video/123456"

# Custom yt-dlp options
./build/bin/ascii-chat mirror --url "https://www.youtube.com/..." --yt-dlp-options "--no-warnings --restrict-filenames"

# Server streaming media to clients
./build/bin/ascii-chat server --file video.mp4
./build/bin/ascii-chat server --url "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

### Caching

Stream URLs extracted by yt-dlp are cached for 30 seconds to avoid repeated subprocess calls during initialization (FPS detection, audio probing, format detection). Cache keys include both URL and yt-dlp options.

### Debugging Media Resolution

```bash
# Watch media resolution process
./build/bin/ascii-chat --log-level debug mirror --file video.mp4 --grep "Media source|resolve|stream"

# Watch yt-dlp execution
./build/bin/ascii-chat --log-level debug mirror --url "https://www.youtube.com/watch?v=..." --grep "yt-dlp"

# Check FFmpeg fallback
./build/bin/ascii-chat --log-level debug mirror --url "https://example.com/unknown-format" --grep "ffmpeg|stream"
```

### Implementation Details

- **lib/media/yt_dlp.c/h** - yt-dlp subprocess integration with caching
- **lib/media/source.c** - Smart routing logic and media source creation
- **include/ascii-chat/options/options.h** - `yt_dlp_options` STRING option

## Cryptographic Protocol

### End-to-End Encryption

ascii-chat uses libsodium for E2E encryption by default:

- **X25519** - Key exchange
- **XSalsa20-Poly1305** - AEAD cipher
- **Ed25519** - SSH key signatures

### Authentication Modes

```bash
# 1. Ephemeral DH (default) - encrypted but no identity verification
./build/bin/ascii-chat server
./build/bin/ascii-chat client

# 2. Password authentication
./build/bin/ascii-chat server --password "secret"
./build/bin/ascii-chat client --password "secret"

# 3. SSH key authentication (TOFU with known_hosts)
./build/bin/ascii-chat server --key ~/.ssh/id_ed25519
./build/bin/ascii-chat client --server-key ~/.ssh/server_id_ed25519.pub

# 4. Fetch keys from GitHub
./build/bin/ascii-chat client --server-key github:zfogg
```

### Debugging Crypto Handshake

```bash
# Watch handshake process
./build/bin/ascii-chat --log-level debug server --grep "handshake"
./build/bin/ascii-chat --log-level debug client --grep "handshake|crypto"

# Check known_hosts verification
./build/bin/ascii-chat --log-level debug client --grep "known_hosts"
```

See `docs/crypto.md` for full protocol specification.

## ACDS (ascii-chat Discovery Service)

The Discovery Service enables automatic session discovery and connection:

### Running ACDS

```bash
# Start discovery server
./build/bin/ascii-chat discovery-service --port 27225 --database ./acds.db

# Register with ACDS (server mode)
./build/bin/ascii-chat server --discovery-service --discovery-service-port 27225

# Find sessions via ACDS (client mode)
./build/bin/ascii-chat client --discovery-service --discovery-service-port 27225
```

### ACDS Features

- Session registration with unique string identifiers (e.g., "blue-mountain-tiger")
- IP privacy (requires explicit `--discovery-expose-ip` confirmation)
- SQLite database for session persistence
- Automatic cleanup of stale sessions

### Debugging ACDS

```bash
# Server side
./build/bin/ascii-chat --log-level dev server --acds --grep "/acds|discovery/ig"

# Client side
./build/bin/ascii-chat --log-level debug client --acds --grep "/acds|discovery/ig"

# ACDS server itself
./build/bin/acds --log-level debug --grep "/session|register/ig"
```

## Network Protocol Debugging

### Packet Structure

Every packet has:

- **Magic**: ASCIICCHAT spelled in hexadecimal as best we could (validation)
- **Type**: packet_type_t enum
- **Length**: payload size
- **CRC32**: payload checksum
- **Client ID**: sender identification

### Common Packet Issues

**magic constant in data**: Stream desynchronization, missing synchronization

```bash
# Debug packet flow
./build/bin/ascii-chat --log-level debug server --grep "/packet|recv|send/ig"
```

**CRC mismatches**: Data corruption or incomplete reads

```bash
# Watch for CRC errors
./build/bin/ascii-chat server --grep "/CRC|checksum/ig"
```

**Unknown packet type**: Client/server version mismatch

```bash
# Check packet types being sent/received
./build/bin/ascii-chat --log-level debug client --grep "/packet_type/ig"
```

## Project Structure

```
ascii-chat/
├── src/                        # Mode entry points and binary initialization
│   ├── main.c                  # Unified binary that routes to mode implementations based on CLI arguments
│   ├── server/                 # Server mode implementation for multi-client video streaming and audio mixing
│   ├── client/                 # Client mode implementation that connects to server and streams webcam/media
│   ├── mirror/                 # Mirror mode for local-only testing of media encoding without networking
│   ├── discovery-service/      # ACDS (ascii-chat Discovery Service) implementation for P2P session discovery
│   ├── discovery/              # Discovery mode client that connects via session strings from ACDS
│   ├── common/                 # Shared code and utilities used by multiple modes
│   ├── session/                # Session initialization and lifecycle management across modes
│   ├── tooling/                # Development and debugging tools integrated into the binary
│   └── web/                    # Web components and WebRTC integration for browser-based clients
├── lib/                        # Core reusable libraries for all modes
│   ├── platform/               # Cross-platform abstractions (threads, mutexes, sockets, file I/O)
│   │   ├── linux/              # Linux-specific implementations using POSIX APIs and system calls
│   │   ├── macos/              # macOS-specific implementations using Cocoa and Darwin APIs
│   │   ├── windows/            # Windows-specific implementations using Win32 APIs
│   │   ├── posix/              # POSIX-compliant implementations for Unix-like systems
│   │   └── wasm/               # WebAssembly implementations for browser environments
│   ├── session/                # Session management UI including splash screens, status displays, and help overlays
│   ├── crypto/                 # Encryption, authentication, SSH key parsing, and cryptographic handshakes
│   │   ├── ssh/                # SSH key parsing, format detection, and serialization utilities
│   │   ├── gpg/                # GPG keyring integration and key management operations
│   │   └── handshake/          # Cryptographic handshake protocol implementation and state machine
│   ├── network/                # Network protocol implementation, packet structure, connection handling, and streaming
│   │   ├── tcp/                # TCP connection management and socket handling
│   │   ├── websocket/          # WebSocket protocol implementation for browser connections
│   │   ├── webrtc/             # WebRTC data channels and media stream handling
│   │   ├── acip/               # Custom ACIP protocol for ASCII chat packet transmission
│   │   ├── mdns/               # mDNS service discovery and local network browsing
│   │   ├── nat/                # NAT traversal and port mapping utilities
│   │   ├── rate_limit/         # Connection rate limiting and traffic shaping
│   │   └── consensus/          # Consensus algorithms for distributed state synchronization
│   ├── video/                  # Webcam capture, V4L2/AVFoundation drivers, ASCII art rendering, and SIMD optimizations
│   │   ├── webcam/             # Webcam capture drivers (V4L2 on Linux, AVFoundation on macOS)
│   │   ├── h265/               # H.265/HEVC video codec support via FFmpeg
│   │   ├── simd/               # SIMD-optimized video processing (AVX2, NEON, etc.)
│   │   ├── scalar/             # Scalar (non-SIMD) video processing fallback implementations
│   │   └── render/             # ASCII art rendering and terminal output generation
│   ├── audio/                  # PortAudio integration for microphone capture, speaker output, and audio mixing
│   ├── media/                  # Media file/URL handling with FFmpeg, yt-dlp integration, and format detection
│   ├── discovery/              # ACDS client library for session registration, lookup, and cleanup
│   ├── log/                    # Centralized logging system with filtering, formatting, and file output
│   ├── options/                # CLI argument parsing with environment variable mapping and validation
│   │   ├── builder/            # Option descriptor building and validation framework
│   │   ├── parsing/            # CLI argument parsing and environment variable resolution
│   │   ├── registry/           # Global option registry and lookup system
│   │   ├── config/             # Configuration file loading and merging
│   │   ├── completions/        # Shell completion generation (bash, zsh, fish)
│   │   └── manpage/            # Man page documentation generation from option descriptors
│   ├── util/                   # Utility functions for strings, IP validation, URLs, and common helpers
│   ├── debug/                  # Memory allocation tracking, sync primitive inspection, and performance profiling
│   ├── common/                 # Shared structures and utilities used across multiple library modules
│   ├── core/                   # Core data structures and abstractions used throughout the codebase
│   ├── ui/                     # Terminal UI components for interactive displays and user feedback
│   ├── tooling/                # Internal tools for code generation, validation, and build-time utilities
│   │   ├── defer/              # Deferred execution and resource cleanup helpers
│   │   ├── panic/              # Panic/crash handling and diagnostic reporting
│   │   └── query/              # Query and introspection tools for runtime analysis
│   ├── tests/                  # Shared test utilities and fixtures for the test suite
│   ├── uthash/                 # Hash table implementation library (vendored dependency)
│   ├── cmake/                  # CMake support libraries and configuration helpers
│   └── pkgconfig/              # pkg-config definition files for library installation
├── cmake/                      # Build system configuration and utilities
│   ├── dependencies/           # CMake modules for finding/configuring external libraries (PCRE2, FFmpeg, etc.)
│   ├── targets/                # Build target definitions and custom commands for code generation
│   ├── tools/                  # CMake utility scripts for template substitution and binary embedding
│   ├── platform/               # Platform-specific CMake configuration (Windows, macOS, Linux)
│   ├── compiler/               # Compiler detection and flag configuration for Clang
│   ├── toolchains/             # CMake toolchain files for cross-compilation and build environments
│   ├── init/                   # CMake initialization and project setup helpers
│   ├── install/                # Installation configuration and packaging rules
│   ├── test/                   # Test framework configuration and test runner setup
│   ├── utils/                  # General-purpose CMake utility macros and functions
│   └── scripts/                # CMake-invoked scripts for build-time operations
├── include/                    # Public header files organized by module
│   └── ascii-chat/             # Main header directory with subdirectories mirroring lib/ structure
├── tests/                      # Criterion test suite with unit and integration tests for all modules
│   └── unit/                   # Unit tests for individual modules organized by component
├── docs/                       # Protocol documentation, architecture guides, and usage examples
├── deploy/                     # Deployment configurations for ACDS, coturn, and cloud infrastructure
├── web/                        # Web applications and browser-based client interfaces
├── images/                     # Project images, screenshots, and visual assets
├── git-hooks/                  # Git hooks for pre-commit checks and automation
├── witness/                    # Test witness files and reference data for regression testing
├── polecats/                   # Project management and tracking utilities
├── vcpkg-overlay/              # Custom vcpkg port definitions for dependency management
└── build/                      # CMake-generated build output (binaries, object files, generated code)
```

## Testing

```bash
# Run all tests
cmake --build build --target tests
ctest --test-dir build --output-on-failure --parallel 0

# Run specific test category
ctest --test-dir build --label-regex "^unit$" --output-on-failure

# Run single test
ctest --test-dir build -R "crypto_handshake" --output-on-failure
```

**Note**: On Intel macOS (not ARM), use Docker for Criterion tests:

```bash
docker-compose -f ./tests/docker-compose.yml run --rm ascii-chat-tests bash -c 'build_docker/bin/test_unit_crypto_handshake'
```

On Linux run the tests directly.
On Windows Criterion isn't supported.

When running Criterion test binaries, run individual tests inside of a test suite by using a filter like `--filter
"*my_test_name_filter*"` (don't forget the asterisks).

## Development Best Practices

### Memory Management Rules

1. Always use `SAFE_MALLOC/CALLOC/FREE` macros
2. Set pointers to NULL after freeing
3. Framebuffers own their data - don't double-free
4. Buffer pool allocations are automatically tracked

### Logging Best Practices

**Important: Never use fprintf() or printf() for debug output**

Always use the logging system (`log_debug()`, `log_info()`, etc.) instead of raw `fprintf()` or `printf()` calls:

```c
// ❌ WRONG - bypasses logging system, causes UI artifacts
fprintf(stderr, "[DEBUG] Processing packet\n");

// ✅ CORRECT - goes through logging system
log_debug("Processing packet");
```

**Why:** Raw fprintf/printf calls:

- Bypass `--grep` filtering (can't isolate specific logs)
- Don't write to log file (can't debug post-mortem)
- Interrupt UI rendering (splash screen flashing, scrolling artifacts)
- Ignore `--quiet` and log level settings

**Additional best practices:**

1. Use `log_*_every()` in high-frequency code (video/audio loops)
2. Use `--grep` to filter noise during debugging
3. Set appropriate log levels: dev < debug < info < warn < error < fatal
4. Log to file with `--log-file` when output would interfere with UI

### Comment Style

**Never use emphatic comment prefixes like "CRITICAL:", "WARNING:", "IMPORTANT:", etc.**

All comments in code are important by nature. Write them in a professional, matter-of-fact tone.

**Acceptable prefixes:**

- ✅ `TODO:` - for planned future work
- ✅ `FIXME:` - for known issues that need fixing
- ❌ `CRITICAL:` - everything in production code is critical
- ❌ `WARNING:` - if it's dangerous, explain why, don't just warn
- ❌ `IMPORTANT:` - all comments should be important
- ❌ `HACK:` - if it's a hack, refactor it or explain the constraints

```c
// ❌ WRONG - emphatic style that doesn't add value
// CRITICAL: This must be called before the mutex is destroyed!
// WARNING: Do not modify this without updating the corresponding code!
// IMPORTANT: Buffer must be freed after use!

// ✅ CORRECT - professional, clear, direct
// This must be called before the mutex is destroyed.
// Do not modify this without updating the corresponding code.
// Buffer must be freed after use.

// ✅ CORRECT - actionable markers for future work
// TODO: Add support for IPv6 addresses
// FIXME: Race condition when multiple threads access this simultaneously
```

**Why:**

- Emphatic prefixes like CRITICAL/WARNING/IMPORTANT make code look unprofessional
- All comments in production code should be equally important and carefully considered
- The comment content itself should convey importance through clear explanation
- If something is truly critical, explain why and what happens if violated

```c
// ❌ WRONG
// CRITICAL: Writer must NOT modify read_index!

// ✅ CORRECT
// Writer must not modify read_index (race condition with reader).
```

### Error Handling Best Practices

1. Always use `asciichat_error_t` return types
2. Use `SET_ERRNO()` for errors with context
3. Use `SET_ERRNO_SYS()` for system call errors
4. Check return values and propagate errors up

### CMake Best Practices

Required dependencies must be resolved at configure time and linked wherever their
symbols are used. Do not wrap required linking in `if(FOO_FOUND)` guards that
silently omit a library. Use `find_package(... REQUIRED)` or the dependency
module's explicit failure checks.

The dependency configuration lives in `cmake/deps/Dependencies.cmake` and its
included modules. `vcpkg.json` records managed packages and version overrides;
source-built dependencies also have configuration in `cmake/deps/`. Consult these
files for platform-specific requirements and pinned versions.

The native build uses:

- **Networking and security:** libuv, libsodium, OpenSSL, BearSSL,
  libdatachannel, libwebsockets, miniupnpc, and mdns.
- **Audio:** PortAudio, Opus, FFTW3, and WebRTC Audio Processing (AEC3),
  with Abseil support.
- **Media and compression:** FFmpeg (`avcodec`, `avformat`, `avutil`,
  `swscale`, and `swresample`), x264/x265 encoder support, and zstd.
- **Data and configuration:** PCRE2, SQLite3, yyjson, tomlc17, and uthash.
- **Terminal rendering:** libvterm, FreeType2, and Fontconfig where supported.

Requirements vary by platform and build target:

- ALSA is a Linux audio dependency; JACK is an optional PortAudio backend.
- miniupnpc supports automatic port mapping with a graceful fallback when
  unavailable; follow its dependency module's platform-specific checks.
- mimalloc is controlled by `USE_MIMALLOC` and is skipped on iOS.
- libexecinfo supplies backtrace support for musl builds.
- Criterion is a test dependency on supported Unix platforms, not Windows.
- Platform libraries, source-build support libraries, and codec dependencies
  are selected by the corresponding CMake modules. Native, WASM, iOS,
  static, and shared builds do not all link the same libraries.

Conditionals are appropriate for actual platform, feature, target, and build
variants. Optional discovery must correspond to a supported fallback or disabled
code path; it must not hide a missing dependency used by compiled code.

```cmake
find_package(PCRE2 REQUIRED)
target_link_libraries(my-target ${PCRE2_LIBRARIES})
```

### Git Workflow

```bash
# Never use git add . or git add -A
git add file1.c file2.c file3.h

# Commit with Co-Authored-By for Claude changes
git commit -m "fix: description
# Don't leave an "claude by anthropic" reference in the commit message
```

### Web Production Deployment

The `ascii-chat-www` Vercel project serves `ascii-chat.com`. Its production
branch is `deploy-web-www`, which starts from the repository default branch
(`master`). Do not deploy production from ordinary `master` pushes.

To publish the latest already-pushed `master` without checking out branches or
disturbing a dirty working tree:

```bash
git fetch origin
git push origin origin/master:deploy-web-www
```

This advances `deploy-web-www` to the current remote `master` commit and triggers
one Vercel production deployment. Keep updates to `deploy-web-www` intentional and
infrequent. Vercel can still create preview deployments for non-production
pushes that change `web/www` or its dependencies; do not rely on Vercel's
Ignored Build Step to avoid deployment quota usage.

## Common Issues

### Build Issues

**"Unknown mode or invalid argument"**: Check command line - mode must come after binary-level options

```bash
# ✅ CORRECT
./build/bin/ascii-chat --log-level info server --port 8080

# ❌ WRONG
./build/bin/ascii-chat server --log-level info --port 8080
```

**SIGTTOU when piping**: Fixed - program now checks terminal control before modifying terminal

**Compilation database for defer tool**: Build system automatically handles this

### Network Issues

**Connection refused**: Check server is running and firewall allows port 27224

```bash
# Test connectivity
nc -zv localhost 27224
```

**Handshake timeout**: Check crypto keys match

```bash
./build/bin/ascii-chat --log-level debug client --grep "handshake"
```

### Memory Issues

**Leaks at exit**: Check memory report. Red numbers indicate leaks. Common causes:

- Missing `SAFE_FREE()` calls
- Circular references
- Thread cleanup issues

Use AddressSanitizer in debug builds to find issues:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0
```

## Environment Variables

- `ASCII_CHAT_QUESTION_PROMPT_RESPONSE` - **Stack-based auto-answer for prompts** (for automation/testing)
  - Single response: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y'`
  - Multiple responses: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y;n;password123'`
  - Optional trailing semicolon: `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y;n;'`
  - Responses are popped from the stack as prompts are encountered
  - After stack is exhausted, program waits for manual input
  - Invalid formats (`;`, `;;`, `;y`) are rejected
- `CLAUDECODE=1` - Optimize output for LLM (set automatically by Claude Code)
- `SSH_AUTH_SOCK` - SSH agent socket for key authentication
- `LOG_LEVEL` - Override log level (DEBUG/INFO/WARN/ERROR/FATAL)
- `ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1` - Show backtraces in memory report

## Resources

- **Crypto Protocol**: `docs/crypto.md`
- **Platform Abstraction**: `lib/platform/README.md`
- **Build System**: `cmake/` directory

## Shared limits

Use `include/ascii-chat/common/limits.h` (also included by `common.h`) for
application limits. Include it directly when only constants are needed.

- `MAX_SESSION_KEYS` bounds keys in one session creation sequence and CLI
  identity-key arrays; it does not cap persisted key-rotation history.
- `MAX_SESSION_PARTICIPANTS` bounds session membership, election, hosting and
  related arrays. `MAX_CLIENTS` derives from it. Validate received counts before
  indexing, allocating or serializing; wire counts are still uint8_t.
- `MAX_HOSTNAME_LENGTH` and `MAX_HOSTNAME_LABEL_LENGTH` are ASCII DNS byte
  limits excluding NUL (253 and 63). Apply them to ICU-normalized ASCII names, not the UTF-8 input.
  `HOSTNAME_BUFFER_SIZE` is the existing 256-byte endpoint/OS hostname capacity,
  including NUL, also used for bracketed IP literals. It is not DNS validation.
- `MAX_URL_LENGTH` excludes NUL and bounds the normalized schemeless URL,
  including its added `http://` prefix. Allocate `URL_BUFFER_SIZE` bytes for
  that representation. Explicit-scheme URLs with allocated components remain
  unrestricted by this scratch-buffer limit; consumers check their own capacity.
- Media URL cache capacities include NUL and only govern cache storage. Packed
  STUN/TURN fields have separate protocol capacities; do not enlarge them to
  match general URL or hostname buffers.

Lengths from `strlen()` are bytes, not UTF-8 character counts. Allocate space
for terminators and formatting prefixes/suffixes explicitly. Keep input checks,
overflow checks before size arithmetic, and snprintf error/truncation checks.
A constant documents a bound; it does not prove that untrusted data obeys it.
Reuse semantic constants rather than replacing unrelated equal numeric values.

Use `hostname_to_ascii()` from `util/url.h` for DNS hostname normalization. It uses
ICU UTS #46 with nontransitional processing, bidi and contextual checks; check its
return value and preserve the original input separately for display. `url_parse()`
returns an ASCII host and preserves the path/query/fragment bytes. IP literals
bypass IDNA. Use the canonical host for DNS, TLS SNI/certificate checks and saved
connection identity; do not Punycode URL paths or session names.

### Buffer capacities

Keep each capacity at its narrowest useful scope:

- `common/limits.h` owns shared application limits. `common/buffer_sizes.h`
  provides generic scratch budgets and encoding widths.
- Module headers own contracts shared by their declarations and callers, such
  as `audio/limits.h`, `crypto/sizes.h`, and `network/acip/acds_sizes.h`.
- Implementation-only budgets belong in file-local defines or enums. Use a
  module/file prefix and describe the representation or storage role.

Move established constant names to the owning header and update callers
instead of adding aliases just for the move. Reuse semantic contracts;
matching numeric values alone do not establish shared ownership.

Document bytes versus elements/samples and whether text includes NUL. Derive
combined storage from its component widths. Use `sizeof(array)` or
`ARRAY_SIZE(array)` for operations on owned arrays, and the owning capacity
for pointer-backed allocations, checks, copies, formatting, and loop bounds.
C fixed array bounds require macros or enum constants rather than local
`const size_t` variables that would create VLAs.

Packed wire storage is a protocol contract. Preserve its width independently
of larger application/OS storage, and distinguish it from validation limits.

Literal dimensions remain appropriate for initialized tuples and test vectors,
FourCC tags, single scalar/plane outputs, reserved wire padding, byte-domain
lookup tables, and SIMD lanes when the surrounding code makes their purpose
clear. Boundary-test sizes and expected wire widths should remain independent
of implementation constants. Generated and vendored sources retain their own
conventions. Arbitrary scratch, queue, parser, and formatting budgets need names.
