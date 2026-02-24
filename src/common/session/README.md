# Session Module Documentation

## Overview

The **session module** (`src/common/session/`) provides a unified abstraction for managing real-time audio-video sessions in ascii-chat. It encapsulates:

- **Server-side session hosting** (client management, media mixing, broadcast)
- **Client-side session participation** (connection management, media streaming)
- **Shared session infrastructure** (capture, rendering, display, audio handling)
- **Interactive controls** (keyboard input, help screens, status displays)

The module is used by all display modes:
- **Server mode**: Hosts sessions with multiple connected clients
- **Client mode**: Connects to a server and streams media
- **Mirror mode**: Local testing without networking
- **Discovery mode**: P2P session discovery and establishment

---

## Architecture

### Core Components

```
┌─────────────────────────────────────────────────────────────┐
│                    Session Module                           │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────────────┐      ┌──────────────────────┐   │
│  │   Host-Side (Server) │      │ Client-Side (Participant) │
│  ├──────────────────────┤      ├──────────────────────┤   │
│  │ session_host_t       │      │ session_participant_t │   │
│  │ - Client management  │      │ - Connection mgmt    │   │
│  │ - Media mixing       │      │ - Stream control     │   │
│  │ - Broadcasting       │      │ - Frame rendering    │   │
│  └──────────────────────┘      └──────────────────────┘   │
│           │                               │                 │
│           └───────────────┬───────────────┘                 │
│                           │                                  │
│           ┌───────────────V────────────────┐               │
│           │   Shared Infrastructure       │               │
│           ├───────────────────────────────┤               │
│           │ - Capture (capture.h)         │               │
│           │ - Display (display.h)         │               │
│           │ - Render loop (render.h)      │               │
│           │ - Audio handling (audio.h)    │               │
│           │ - Keyboard input (keyboard_handler.h) │       │
│           │ - Session settings (settings.h)      │       │
│           │ - Consensus protocol (consensus.h)   │       │
│           │ - Logging (session_log_buffer.h)     │       │
│           └───────────────────────────────┘               │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Component Breakdown

#### **Host-Side (`host.h`)**
Server abstractions for managing client sessions:
- **Lifecycle**: Create → Start → Stop → Destroy
- **Client Management**: Add, remove, find, list clients
- **Media Injection**: For host's own webcam/audio participation
- **Broadcasting**: Send ASCII frames to individual clients or broadcast to all
- **Callbacks**: Join/leave events, frame/audio received, errors
- **Transport Abstraction**: Support for TCP sockets, WebRTC DataChannels, WebSockets

#### **Client-Side (`participant.h`)**
Client abstractions for connecting to sessions:
- **Lifecycle**: Create → Connect → Disconnect → Destroy
- **Stream Control**: Start/stop video and audio streaming
- **Media Reception**: Receive mixed ASCII frames and audio from server
- **Callbacks**: Connected/disconnected events, frame/audio received, errors
- **Reconnection**: Automatic reconnection on network failure

#### **Shared Client-Like Framework (`client_like.h`)**
Unified initialization framework for client-like modes:
- Handles boilerplate setup for mirror, client, and discovery modes
- Media source selection and FPS detection
- Audio initialization and lifecycle
- Terminal management and keepawake
- Splash screen lifecycle
- Keyboard input integration
- Proper resource cleanup ordering

---

## Session Lifecycle

### Host-Side Lifecycle

```
1. Configuration
   session_host_config_t config = { ... };

2. Creation
   host = session_host_create(&config);
   → Host is ready but not accepting connections

3. Start Server
   session_host_start(host);
   → Listen for incoming client connections
   → Accept loop running

4. Client Connection
   session_host_add_client(host, socket, ip, port);
   → on_client_join() callback fired
   → Client registered and ready

5. Media Handling
   session_host_inject_frame(host, client_id, frame);
   → Frame from host's own webcam

   On frame received from client:
   → on_frame_received() callback

6. Rendering (Optional)
   session_host_start_render(host);
   → Render thread mixing video/audio
   → Broadcasting frames to all clients

7. Client Disconnection
   session_host_remove_client(host, client_id);
   → on_client_leave() callback fired

8. Stop Server
   session_host_stop(host);
   → Stop accepting connections
   → Disconnect remaining clients

9. Cleanup
   session_host_destroy(host);
   → Free all resources
```

### Client-Side Lifecycle

```
1. Configuration
   session_participant_config_t config = { ... };

2. Creation
   participant = session_participant_create(&config);
   → Participant object created

3. Connection
   session_participant_connect(participant);
   → Establish connection to server
   → Crypto handshake
   → on_connected() callback fired with assigned ID

4. Stream Control
   session_participant_start_video(participant);
   session_participant_start_audio(participant);
   → Begin streaming media to server

5. Receiving
   Continuously receive frames/audio via callbacks:
   → on_frame_received()
   → on_audio_received()

6. Stream Control
   session_participant_stop_video(participant);
   session_participant_stop_audio(participant);
   → Stop streaming (optional)

7. Disconnection
   session_participant_disconnect(participant);
   → Close connection to server
   → on_disconnected() callback fired

8. Cleanup
   session_participant_destroy(participant);
   → Free all resources
```

### Client-Like Mode Lifecycle

```
1. Configuration
   session_client_like_config_t config = {
     .media_source = { ... },
     .run_fn = mode_specific_main_loop,
     ...
   };

2. Initialization
   session_client_like_run(&config)
   → Terminal output check
   → Keepawake system
   → Media source selection
   → FPS probing
   → Audio initialization
   → Display creation
   → Splash screen animation

3. Mode-Specific Loop
   mode_specific_main_loop(capture, display, user_data)
   → Network loop (client mode)
   → Render loop (mirror/discovery)
   → Keyboard input handling
   → Protocol handling

4. Cleanup (Automatic)
   ← Teardown in correct order:
   → Stop render loop
   → Close display
   → Shutdown audio
   → Close media
   → Disable keepawake
   → Restore terminal
```

---

## Shared Data Structures

### Opaque Handles

These are opaque forward-declared types. Implementation details are hidden.

```c
// Host side
typedef struct session_host session_host_t;

// Client side
typedef struct session_participant session_participant_t;

// Shared
typedef struct session_capture_ctx session_capture_ctx_t;
typedef struct session_display_ctx session_display_ctx_t;
typedef struct session_log_buffer session_log_buffer_t;
```

### Configuration Structures

#### **Host Configuration** (`host.h`)
```c
typedef struct {
  int port;                           // Listen port (27224)
  const char *ipv4_address;          // IPv4 bind address
  const char *ipv6_address;          // IPv6 bind address
  int max_clients;                   // Max concurrent clients
  bool encryption_enabled;            // E2E encryption
  const char *key_path;              // Server identity key
  const char *password;              // Optional password auth
  session_host_callbacks_t callbacks; // Event callbacks
  void *user_data;                   // Context for callbacks
} session_host_config_t;
```

#### **Participant Configuration** (`participant.h`)
```c
typedef struct {
  const char *address;               // Server address
  int port;                          // Server port
  bool encryption_enabled;            // E2E encryption
  const char *key_path;              // Client key (optional)
  const char *password;              // Password auth (optional)
  session_participant_callbacks_t callbacks;
  void *user_data;                   // Context for callbacks
} session_participant_config_t;
```

#### **Client-Like Configuration** (`client_like.h`)
```c
typedef struct {
  session_media_source_t media_source;  // What to capture
  session_client_like_run_fn run_fn;    // Mode-specific loop
  void *run_user_data;                  // Context for run_fn
  // ... audio, display, keyboard config ...
} session_client_like_config_t;
```

### Callback Structures

#### **Host Callbacks**
```c
typedef struct {
  void (*on_client_join)(session_host_t *h, uint32_t client_id, void *data);
  void (*on_client_leave)(session_host_t *h, uint32_t client_id, void *data);
  void (*on_frame_received)(session_host_t *h, uint32_t client_id,
                           const image_t *frame, void *data);
  void (*on_audio_received)(session_host_t *h, uint32_t client_id,
                           const float *samples, size_t count, void *data);
  void (*on_error)(session_host_t *h, asciichat_error_t error,
                  const char *message, void *data);
} session_host_callbacks_t;
```

#### **Participant Callbacks**
```c
typedef struct {
  void (*on_connected)(session_participant_t *p, uint32_t id, void *data);
  void (*on_disconnected)(session_participant_t *p, void *data);
  void (*on_frame_received)(session_participant_t *p, const char *frame, void *data);
  void (*on_audio_received)(session_participant_t *p,
                           const float *samples, size_t count, void *data);
  void (*on_error)(session_participant_t *p, asciichat_error_t error,
                  const char *message, void *data);
} session_participant_callbacks_t;
```

### Log Entry Structure

```c
typedef struct {
  char message[SESSION_LOG_LINE_MAX];  // Formatted log (with ANSI colors)
  uint64_t sequence;                  // Monotonic ordering sequence
} session_log_entry_t;
```

---

## Common Patterns and Utilities

### Capture (`capture.h`)

**Purpose**: Abstract media source reading (webcam, files, URLs)

**Key Functions**:
- `session_capture_open()` - Open media source
- `session_capture_close()` - Close media source
- `session_capture_read_frame()` - Get next frame
- `session_capture_sleep_for_fps()` - Frame rate limiting
- `session_capture_toggle_pause()` - Pause/resume
- `session_capture_seek_relative()` - Seek in files

**Supported Sources**:
- Webcam (platform-specific)
- Local files (MP4, MKV, etc. via FFmpeg)
- URLs (HTTP/HTTPS/RTSP/DASH/HLS)
- YouTube/TikTok (via yt-dlp)

### Display (`display.h`)

**Purpose**: Terminal abstraction and ASCII rendering

**Key Functions**:
- `session_display_create()` - Initialize terminal
- `session_display_destroy()` - Cleanup terminal
- `session_display_convert_to_ascii()` - Convert frames to ASCII
- `session_display_render_frame()` - Draw to terminal
- `session_display_set_color_mode()` - Set palette (mono/16/256/true color)

**Features**:
- Terminal capability detection (ANSI support)
- Color palette selection
- Snapshot mode support
- Help screen integration

### Render Loop (`render.h`)

**Purpose**: Unified frame processing for all display modes

**Two Modes**:

1. **Synchronous Mode** (with capture context)
   ```c
   session_render_loop(capture, display, should_exit_fn,
                      NULL, NULL, keyboard_handler, user_data);
   ```
   - Capture → ASCII conversion → Render loop
   - Frame rate driven by media source

2. **Event-Driven Mode** (with callbacks, no capture)
   ```c
   session_render_loop(NULL, display, should_exit_fn,
                      capture_fn, sleep_fn, keyboard_handler, user_data);
   ```
   - Custom frame/sleep callbacks
   - Protocol-driven (client mode)

### Audio (`audio.h`)

**Purpose**: Audio capture, processing, and playback

**Key Functions**:
- `session_audio_capture_init()` - Start audio input
- `session_audio_capture_read()` - Get audio samples
- `session_audio_playback_init()` - Start audio output
- `session_audio_playback_write()` - Send audio samples
- Volume control and mixing

**Features**:
- Platform abstraction (PortAudio)
- Format conversion (PCM float)
- Volume normalization
- Optional JACK audio system support

### Keyboard Input (`keyboard_handler.h`)

**Purpose**: Interactive session controls

**Supported Controls**:
- `?` - Toggle help screen
- ` ` (spacebar) - Play/pause
- `←` / `→` - Seek ±30s
- `↑` / `↓` - Volume ±10%
- `m` - Mute toggle
- `c` - Cycle color modes
- `f` - Flip webcam (mirror mode)

**Thread-Safe**: Can be called from any thread

### Session Log Buffer (`session_log_buffer.h`)

**Purpose**: Capture logs for status/splash screens

**Functions**:
- `session_log_buffer_init()` - Create circular buffer
- `session_log_buffer_append()` - Add log entry
- `session_log_buffer_get_recent()` - Retrieve entries
- `session_log_buffer_clear()` - Clear all entries

**Features**:
- Thread-safe circular buffer
- ANSI color support
- Automatic sequence numbering

### Settings (`settings.h`)

**Purpose**: Session-wide configuration and state

**Manages**:
- Color mode
- Pause state
- Volume level
- Flip state (for webcam)
- Help screen visibility

**Thread-Safe**: Uses RCU (Read-Copy-Update) for atomic access

---

## State Management

### Host State Transitions

```
INIT → RUNNING → STOPPED → DESTROYED

With clients:
RUNNING:
  - Clients can join/leave
  - Media accepted from clients
  - Frames broadcast to all
  - Optional rendering thread
```

### Participant State Transitions

```
INIT → CONNECTING → CONNECTED → STREAMING → DISCONNECTED → DESTROYED

CONNECTED:
  - Ready to send/receive frames
  - Streams can be started/stopped

STREAMING:
  - Actively sending/receiving media
```

### Settings State (Thread-Safe)

All settings use RCU for lock-free reads:

```c
// Read (lock-free)
session_settings_get_volume();

// Write (brief lock, copies old data)
session_settings_set_color_mode(COLOR_MODE_256);
```

---

## Encryption and Authentication

### Default Behavior

All connections are **encrypted by default** using:
- **X25519** - Key exchange
- **XSalsa20-Poly1305** - AEAD symmetric cipher
- **Ed25519** - Signature verification

### Authentication Methods

1. **Ephemeral DH** (default)
   - Encrypted but no identity verification
   - Keys negotiated at connection time

2. **Password Authentication**
   - Both sides prove knowledge of password
   - No long-term keys needed

3. **SSH Key Authentication**
   - Server authenticates with Ed25519 key
   - Client verifies against known_hosts
   - TOFU (Trust On First Use) supported

### Configuration

```c
// Ephemeral (default)
config.encryption_enabled = true;

// Password
config.password = "secret";

// SSH key
config.key_path = "~/.ssh/id_ed25519";

// Disable (testing only)
config.encryption_enabled = false;
```

---

## Error Handling

All functions return `asciichat_error_t`:

```c
asciichat_error_t result = session_host_start(host);
if (result != ASCIICHAT_OK) {
  asciichat_error_context_t ctx;
  if (HAS_ERRNO(&ctx)) {
    log_error("Failed: %s", ctx.context_message);
  }
  return result;
}
```

---

## Consensus Protocol (`consensus.h`)

The session module implements a **consensus protocol** for coordinating state across participants:

### Purpose

- Synchronize pause/play state
- Coordinate media timing
- Handle late-join clients
- Support participant discovery

### Key Concepts

- **Sequence Numbers**: Track state changes
- **Vector Clocks**: Causal ordering
- **Gossip Protocol**: Distributed state propagation

---

## Typical Usage Patterns

### Server with Client Hosting

```c
// Create server host
session_host_config_t config = {
  .port = 27224,
  .max_clients = 32,
  .callbacks = {
    .on_client_join = handle_client_join,
    .on_frame_received = handle_frame,
    // ...
  },
  .user_data = app_context
};
session_host_t *host = session_host_create(&config);

// Start accepting clients
session_host_start(host);

// Add host's own participation (optional)
uint32_t host_id = session_host_add_memory_participant(host);

// Start rendering thread (mixes video/audio)
session_host_start_render(host);

// Cleanup
session_host_stop_render(host);
session_host_stop(host);
session_host_destroy(host);
```

### Client Connection

```c
// Create client participant
session_participant_config_t config = {
  .address = "127.0.0.1",
  .port = 27224,
  .callbacks = {
    .on_connected = handle_connected,
    .on_frame_received = render_frame,
    // ...
  },
  .user_data = app_context
};
session_participant_t *p = session_participant_create(&config);

// Connect to server
session_participant_connect(p);

// Start streaming (automatic after connect)
session_participant_start_video(p);
session_participant_start_audio(p);

// Cleanup
session_participant_disconnect(p);
session_participant_destroy(p);
```

### Mirror/Discovery Mode (via client_like)

```c
// Configure media source
session_media_source_t source = {
  .type = MEDIA_SOURCE_WEBCAM
};

// Configure client-like framework
session_client_like_config_t config = {
  .media_source = source,
  .run_fn = my_render_loop,
  .run_user_data = my_context,
  // ... audio, display config ...
};

// Run unified initialization and mode loop
asciichat_error_t result = session_client_like_run(&config);
// All initialization, teardown, and cleanup handled automatically
```

---

## Key Implementation Details

### Thread Safety

**Host**: Thread-safe for all operations
- Client operations are atomic
- Callbacks can be from any thread
- Safe to call from audio/video threads

**Participant**: Thread-safe for stream control
- Send operations are async
- Receive callbacks from dedicated thread
- Settings updates are RCU-protected

**Render Loop**: Single-threaded by design
- Keyboard input can be called from any thread
- Frame operations must be synchronized by caller

### Memory Management

All allocations tracked with `SAFE_MALLOC/CALLOC/FREE`:
- Automatic leak detection in debug builds
- Memory reports printed at exit

### Event Loop Integration

Host and participant both provide callback-based interfaces for integration with custom event loops:
- No internal threads required
- Can be used with epoll/kqueue/select
- Suitable for embedded systems

---

## Files Overview

| File | Purpose |
|------|---------|
| **host.h** | Server session management |
| **participant.h** | Client session participation |
| **client_like.h** | Unified client-like mode framework |
| **capture.h** | Media source abstraction |
| **display.h** | Terminal abstraction and rendering |
| **render.h** | Unified render loop |
| **audio.h** | Audio capture/playback |
| **keyboard_handler.h** | Interactive controls |
| **session_log_buffer.h** | Log capture for screens |
| **settings.h** | Thread-safe session settings |
| **consensus.h** | State coordination protocol |
| **consensus_integration.h** | Consensus integration helpers |

---

## See Also

- **Crypto Protocol**: `docs/crypto.md` - Detailed cryptographic handshake
- **Network Protocol**: `lib/network/` - ACIP packet protocol
- **Platform Abstraction**: `lib/platform/` - Cross-platform internals
- **Media Resolution**: `lib/media/` - FFmpeg/yt-dlp integration
