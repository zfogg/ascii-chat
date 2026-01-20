# Discovery Mode: Direct P2P Calling for ascii-chat

## Philosophy

ascii-chat should be as simple as making a phone call. Today, users need to understand server/client architecture, port forwarding, and NAT traversal to have a video call. The new **Discovery Mode** changes this:

```bash
# Alice starts a call
$ ascii-chat
Session: swift-river-mountain
Waiting for others to join...

# Bob joins
$ ascii-chat swift-river-mountain
Connecting to Alice...
```

That's it. No servers to configure, no ports to forward, no technical knowledge required.

### Core Principles

1. **One command to start, one command to join** - The default `ascii-chat` invocation starts a session
2. **Best connection wins** - Automatically negotiate who hosts based on NAT quality
3. **Settings travel with the session** - The person who starts sets the options (mutable mid-session)
4. **Graceful degradation** - Try direct connection first, fall back through STUN/TURN
5. **Server mode still exists** - Power users can still run dedicated servers

## Current Architecture

### What We Have

```
src/
├── server/      # Multi-client video server (runs on host)
├── client/      # Video client (connects to server)
├── mirror/      # Local webcam preview (no network)
└── discovery-server/  # ACDS session management
```

**Key insight**: The server and client code are tightly coupled to their modes. To call someone, you currently need:
1. One person runs `ascii-chat server --acds` to register with ACDS
2. Other person runs `ascii-chat swift-river-mountain` to connect

This works, but requires users to decide who's the "server" upfront.

### What We Need

A new **Discovery Mode** where:
1. Both users run essentially the same command
2. NAT quality determines who becomes the host
3. The "loser" of NAT negotiation runs client software
4. The "winner" runs server software AND participates as a client
5. Options set by session creator propagate to all participants (and are mutable)

## NAT Quality Detection

### Dual Detection Strategy

We use **two complementary methods** for NAT detection:

1. **ICE Candidate Gathering** (WebRTC native)
   - Already happens during WebRTC setup
   - Reveals candidate types: host, srflx (server-reflexive), relay
   - If srflx candidate IP == local IP → we have public IP
   - If only relay candidates → we're behind symmetric NAT

2. **NAT_QUALITY Packet** (rich metadata)
   - Explicit UPnP/NAT-PMP availability (not visible in ICE)
   - STUN latency measurements
   - LAN reachability flags (same subnet detection)
   - **Bandwidth measurements** (upload speed matters for hosting)
   - Allows deterministic host selection before ICE completes

Both are used: ICE for actual connectivity, NAT_QUALITY for informed host selection.

### Bandwidth Detection

Upload bandwidth is critical for hosting - a host streams video to N clients simultaneously. We measure it two ways:

1. **ACDS Upload Test** (during session create/join)
   - Client uploads small payload (e.g., 64KB) to ACDS
   - ACDS measures receive rate, reports back in SESSION_CREATED/SESSION_JOINED
   - Quick (~100ms), happens during initial handshake
   - Provides baseline upload speed

2. **Protocol Timing Statistics** (ongoing)
   - Track packet RTT from ping/pong exchanges
   - Measure actual throughput during NAT_QUALITY exchange
   - Historical stats from previous sessions (if available)
   - Detect jitter and packet loss patterns

**Why upload speed matters:**
- Host sends video frames to ALL clients (upload-bound)
- Client only sends their own video to host (less demanding)
- Someone with 100Mbps down / 5Mbps up should NOT host over someone with 50Mbps down / 50Mbps up

### Priority Order (Best to Worst)

| Priority | Connection Type | Detection Method | Who Hosts |
|----------|-----------------|------------------|-----------|
| 0 | Localhost/LAN | mDNS discovery, same subnet | Either (prefer initiator) |
| 1 | Public IP | STUN reflexive == local IP | User with public IP |
| 2 | UPnP/NAT-PMP | Port mapping success | User who can map ports |
| 3 | STUN (hole-punch) | ICE connectivity check | Better NAT type |
| 4 | TURN relay | Always works | Initiator (tiebreaker) |

**Bandwidth as Tiebreaker and Override:**
- Within same NAT priority tier: higher upload bandwidth wins
- **Override rule**: If bandwidth difference is >10x, lower NAT tier can win
  - Example: Public IP with 2 Mbps upload loses to UPnP with 50 Mbps upload
  - Prevents terrible hosting experience from "better" NAT type

**Note on UPnP**: ~70% of home routers support it, and we already have it implemented. It stays at priority 2.

## Timing & Timeouts

### Initial Connection Timeline

```
Alice starts session                              Bob joins
    |                                                 |
    |-- Connect to ACDS ------> [50-200ms]            |
    |-- Bandwidth test -------> [100-500ms]           |
    |-- SESSION_CREATE -------> [~10ms]               |
    |<- SESSION_CREATED -------- [~10ms]              |
    |                                                 |
    |   "Session: swift-river-mountain"               |
    |   [Alice waits, shows session string]           |
    |                                                 |
    |                           [Bob enters string]   |
    |                                                 |
    |                           |-- Connect ACDS --> [50-200ms]
    |                           |-- Bandwidth test -> [100-500ms]
    |                           |-- SESSION_JOIN ---> [~10ms]
    |                           |<- SESSION_JOINED -- [~10ms]
    |                           |   host_established: false
    |                                                 |
    |   [Both start NAT probing in parallel]          |
    |                                                 |
    |-- STUN probe -----------> [50-200ms]  <-------- STUN probe --|
    |-- UPnP probe -----------> [100-1000ms] <------- UPnP probe --|
    |                                                 |
    |<========= NAT_QUALITY exchange ========> [~100ms round-trip]
    |                                                 |
    |   [Both compute winner - instant]               |
    |                                                 |
    |-- HOST_ANNOUNCEMENT ----> [~10ms] (if winner)   |
    |<- HOST_DESIGNATED ------- [~10ms]               |
    |                                                 |
    |   [Winner starts server] [~100ms]               |
    |   [Loser connects]       [50-200ms]             |
    |                                                 |
    |<=============== CALL STARTS ===================>|

TOTAL: ~1.5-3.5 seconds from Bob pressing enter to call starting
```

### Migration Timeline (Host Disconnects)

```
Host crashes                All participants
    X                           |
    |                           |
    |   [Detect disconnect]     |
    |   - TCP RST: immediate    |
    |   - Timeout: up to 30s    |
    |                           |
    |   [Each participant independently:]
    |   |-- STUN probe -------> [50-200ms]
    |   |-- UPnP probe -------> [100-1000ms]
    |   |-- Bandwidth test ---> [100-500ms]
    |   |-- HOST_LOST --------> [~10ms]
    |                           |
    |   [ACDS collection window]
    |   - Fast: all report ---> [0-1000ms]
    |   - Slow: timeout ------> [5000ms max]
    |                           |
    |   [ACDS elects host]      [instant]
    |                           |
    |<- HOST_DESIGNATED -------- [~10ms]
    |                           |
    |   [Winner starts server]  [~100ms]
    |   [Others connect]        [50-200ms]
    |                           |
    |<======== CALL RESUMES ===================>|

TOTAL: ~1.5-6.5 seconds (fast path ~1.5s, timeout path ~6.5s)
       + disconnect detection time (0-30s depending on failure mode)
```

### Timeout Constants

```c
// Connection timeouts
#define ACDS_CONNECT_TIMEOUT_MS      5000    // TCP connect to ACDS
#define ACDS_HANDSHAKE_TIMEOUT_MS    3000    // Complete handshake after connect
#define BANDWIDTH_TEST_TIMEOUT_MS    2000    // Upload test to ACDS

// NAT probing timeouts
#define STUN_PROBE_TIMEOUT_MS        2000    // STUN binding request
#define UPNP_PROBE_TIMEOUT_MS        3000    // UPnP/NAT-PMP discovery + mapping
#define NAT_QUALITY_EXCHANGE_MS      2000    // Peer NAT_QUALITY round-trip

// Session timeouts
#define SESSION_JOIN_TIMEOUT_MS      10000   // Max time to join after SESSION_JOIN sent
#define HOST_NEGOTIATION_TIMEOUT_MS  5000    // Max time for initial 2-party negotiation

// Migration timeouts
#define MIGRATION_WINDOW_MS          5000    // Candidate collection window
#define MIGRATION_MIN_CANDIDATES     2       // Minimum to proceed
#define HOST_STARTUP_TIMEOUT_MS      3000    // New host must be ready within this

// Keepalive / disconnect detection
#define KEEPALIVE_INTERVAL_MS        5000    // Ping frequency
#define KEEPALIVE_TIMEOUT_MS         15000   // Miss 3 pings = disconnect
#define TCP_USER_TIMEOUT_MS          30000   // TCP-level timeout (RFC 5482)
```

### User-Visible Latency Summary

| Scenario | Best Case | Typical | Worst Case |
|----------|-----------|---------|------------|
| Start session (to string shown) | 200ms | 500ms | 1.5s |
| Join session (to call starts) | 1s | 2s | 5s |
| Host migration (to call resumes) | 1.5s | 3s | 6.5s + detection |
| Disconnect detection | 0ms (RST) | 5s | 30s (timeout) |

### Optimizations for Speed

1. **Parallel probing**: STUN, UPnP, and bandwidth tests run concurrently
2. **Optimistic UI**: Show "Connecting..." immediately, don't block on all probes
3. **Cached NAT info**: Remember NAT type from previous sessions (valid ~5 min)
4. **Fast-path migration**: If all participants report quickly, skip full 5s window
5. **Preemptive STUN**: Start STUN probe on session join before knowing if needed

## Session Flow & Order of Operations

### Critical Question: What Data Do We Need First?

Before any action, we need to know:
1. **Does the session exist?** (from ACDS)
2. **Is a host already established?** (from ACDS)
3. **If no host**: Other participants' NAT quality + ICE candidates
4. **If host exists**: Host's connection info (address, port)

This determines whether we negotiate or just connect.

### Flow: First Two Participants (Host Negotiation Required)

```
Alice                          ACDS                           Bob
  |                              |                              |
  |-- SESSION_CREATE ----------->|                              |
  |<- SESSION_CREATED -----------|                              |
  |   session_id, participant_id |                              |
  |   session_string             |                              |
  |                              |                              |
  |   [Alice displays session string, waits]                    |
  |                              |                              |
  |                              |<------- SESSION_JOIN ------- |
  |                              |                              |
  |                              |-------- SESSION_JOINED ---->|
  |                              |   host_established: false    |
  |                              |   peers: [Alice]             |
  |                              |                              |
  |<-- PARTICIPANT_JOINED -------|   (notify Alice of Bob)      |
  |                              |                              |
  |============ ICE Candidate Gathering (parallel) ============|
  |                              |                              |
  |<======== NAT_QUALITY exchange (via ACDS relay) ===========>|
  |   Alice: {public_ip: no, upnp: yes, nat: restricted}       |
  |   Bob:   {public_ip: yes, upnp: no, nat: open}             |
  |                              |                              |
  |   [Both run same algorithm: Bob wins (public IP)]          |
  |                              |                              |
  |                              |<----- HOST_ANNOUNCEMENT ---- |
  |                              |   host_id: Bob               |
  |                              |   host_address: 1.2.3.4      |
  |                              |   host_port: 27224           |
  |                              |                              |
  |<-- HOST_DESIGNATED ---------|-------- HOST_DESIGNATED ---->|
  |   host_id: Bob              |   host_id: Bob (confirmation)|
  |   host_address: 1.2.3.4     |                              |
  |                              |                              |
  |   [Bob starts server + joins as participant]               |
  |   [Alice connects as client]                               |
  |                              |                              |
  |<======== SETTINGS_SYNC (Alice's options to Bob) ==========>|
  |                              |                              |
  |<=============== Video call in progress ===================>|
```

### Flow: Third+ Participant (Host Already Established)

```
Carol                          ACDS                     Host (Bob)
  |                              |                              |
  |-- SESSION_JOIN ------------->|                              |
  |   session_string             |                              |
  |                              |                              |
  |<- SESSION_JOINED ------------|                              |
  |   host_established: true     |                              |
  |   host_id: Bob               |                              |
  |   host_address: 1.2.3.4      |                              |
  |   host_port: 27224           |                              |
  |                              |                              |
  |   [No NAT negotiation needed - just connect to host]       |
  |                              |                              |
  |-- Direct TCP or WebRTC ---------------------------------->|
  |                              |                              |
  |<============ SETTINGS_SYNC (via host) ===================>|
  |                              |                              |
  |<=============== Video call in progress ===================>|
```

### Flow: Host Disconnects (Migration)

When the host disconnects, **all** remaining participants reconnect to ACDS, report their NAT/bandwidth, and ACDS elects the best new host from the entire pool.

#### Migration Flow

```
                              ACDS                     Host (Bob, disconnects)
Alice    Carol    Dave          |                              |
  |        |        |           |            [Bob disconnects or crashes]
  |        |        |           |                              X
  |        |        |           |
  |   [All detect TCP connection lost]
  |        |        |           |
  |-- HOST_LOST + NAT_QUALITY ->|
  |   upload: 25 Mbps           |
  |   nat: upnp                 |
  |        |        |           |
  |        |-- HOST_LOST ------>|
  |        |   + NAT_QUALITY    |
  |        |   upload: 50 Mbps  |
  |        |   nat: public_ip   |
  |        |        |           |
  |        |        |-- HOST_LOST + NAT_QUALITY -->|
  |        |        |   upload: 10 Mbps            |
  |        |        |   nat: stun                  |
  |        |        |           |
  |   [ACDS waits for collection window: 5s or all known participants]
  |        |        |           |
  |   [ACDS computes best host from ALL candidates]
  |   [Carol wins: public IP + highest upload bandwidth]
  |        |        |           |
  |<-- HOST_DESIGNATED ---------|--------- HOST_DESIGNATED -->|
  |   host: Carol               |   host: Carol (you!)        |
  |   addr: 1.2.3.4:27224       |   (start server)            |
  |        |        |           |                             |
  |        |        |<--------- HOST_DESIGNATED --------------|
  |        |        |   host: Carol                           |
  |        |        |   addr: 1.2.3.4:27224                   |
  |        |        |           |
  |   [Carol starts server + joins as participant]
  |   [Alice and Dave connect to Carol]
  |        |        |           |
  |<=============== Video call resumes =======================>|
```

#### Key Insight: Everyone Reports, ACDS Decides

Unlike initial session negotiation (where only 2 participants exist), migration considers **all** remaining participants:

1. **Each participant independently**:
   - Detects host disconnect (TCP connection lost)
   - Reconnects to ACDS with HOST_LOST
   - Runs bandwidth test (64KB upload to ACDS)
   - Runs STUN/UPnP checks to determine NAT type
   - Sends NAT_QUALITY with results

2. **ACDS collects candidates**:
   - Tracks all HOST_LOST reports for this session
   - Waits for collection window (5 seconds or all known participants)
   - Has complete picture of everyone's NAT + bandwidth

3. **ACDS elects best host**:
   - Runs same `nat_compare_quality()` algorithm
   - Compares ALL candidates, not just first two
   - Picks winner with best combination of NAT tier + bandwidth
   - Sends HOST_DESIGNATED to everyone simultaneously

#### Collection Window

ACDS uses a hybrid timeout strategy:

```c
#define MIGRATION_WINDOW_MS       5000   // Max wait time
#define MIGRATION_MIN_CANDIDATES  2      // Need at least 2 to have a call

// Collection ends when EITHER:
// 1. All known participants have reported (fast path)
// 2. MIGRATION_WINDOW_MS elapsed AND we have MIN_CANDIDATES (timeout path)
// 3. Session had N participants, N-1 have reported (host was Nth)
```

#### NAT Probing During Migration

Each participant determines their NAT type independently:

```
Participant                    STUN Server              ACDS
    |                              |                      |
    |-- STUN Binding Request ----->|                      |
    |<-- Binding Response ---------|                      |
    |   (public IP, port)          |                      |
    |                              |                      |
    |-- UPnP/NAT-PMP probe ------->| (router)             |
    |<-- Port mapping result ------|                      |
    |                              |                      |
    |-- Bandwidth test (64KB) ---------------------------->|
    |<-- Bandwidth result ---------------------------------|
    |                              |                      |
    |-- HOST_LOST + NAT_QUALITY -------------------------->|
    |   (all measurements included)                       |
```

This is **faster** than peer-to-peer probing because:
- STUN/UPnP checks happen in parallel across all participants
- No need to coordinate pairwise NAT traversal
- ACDS already has connection to everyone

#### Migration State Machine

```
SESSION_ACTIVE (host exists)
    |
    v [any participant reports HOST_LOST]
SESSION_MIGRATING
    |-- candidates[] (participants with NAT_QUALITY)
    |-- known_participants (from previous session state)
    |-- collection_start_time
    |
    v [collection complete OR timeout with MIN_CANDIDATES]
ELECTING_HOST
    |-- run nat_compare_quality() on all candidates
    |-- pick winner
    |
    v [send HOST_DESIGNATED to all]
SESSION_ACTIVE (new host)
```

#### Edge Cases

- **Only one participant left**: Session ends (can't have a call alone)
- **No candidates after timeout**: Session ends, participants notified
- **New joiner during migration**: Added to candidates[], included in election
- **Elected host fails to start**: ACDS detects, re-runs election excluding them
- **Network partition**: Participants on different sides may elect different hosts
  - Resolved when partition heals (one session survives, others merge or end)

## ACIP Protocol Extensions

### New Packet Types

```c
// NAT quality exchange (user-to-user, relayed via ACDS)
PACKET_TYPE_ACIP_NAT_QUALITY = 130,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];

  // NAT detection results
  uint8_t has_public_ip;          // STUN reflexive == local IP
  uint8_t upnp_available;         // UPnP/NAT-PMP port mapping works
  uint8_t upnp_mapped_port[2];    // Port we mapped (network byte order)
  uint8_t stun_nat_type;          // 0=open, 1=full-cone, 2=restricted, 3=port-restricted, 4=symmetric
  uint8_t lan_reachable;          // Same subnet as peer (mDNS/ARP)
  uint32_t stun_latency_ms;       // RTT to STUN server

  // Bandwidth measurements (critical for host selection)
  uint32_t upload_kbps;           // Upload bandwidth in Kbps (from ACDS test)
  uint32_t download_kbps;         // Download bandwidth in Kbps (informational)
  uint16_t rtt_to_acds_ms;        // Latency to ACDS server
  uint8_t jitter_ms;              // Packet timing variance (0-255ms)
  uint8_t packet_loss_pct;        // Packet loss percentage (0-100)

  // Connection info
  char public_address[64];        // Our public IP (if has_public_ip or upnp)
  uint16_t public_port;           // Our public port

  // ICE candidate summary (complements ICE gathering)
  uint8_t ice_candidate_types;    // Bitmask: 1=host, 2=srflx, 4=relay
} acip_nat_quality_t;

// Host announcement (participant -> ACDS, "I won negotiation, I'm hosting")
PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT = 131,
typedef struct {
  uint8_t session_id[16];
  uint8_t host_id[16];            // My participant ID
  char host_address[64];          // Where clients should connect
  uint16_t host_port;             // Port
  uint8_t connection_type;        // 0=direct_public, 1=upnp, 2=stun, 3=turn
} acip_host_announcement_t;

// Host designated (ACDS -> all participants, "here's your host")
PACKET_TYPE_ACIP_HOST_DESIGNATED = 132,
typedef struct {
  uint8_t session_id[16];
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;
  uint8_t connection_type;
} acip_host_designated_t;

// Settings synchronization (initiator -> all, mutable mid-session)
PACKET_TYPE_ACIP_SETTINGS_SYNC = 133,
typedef struct {
  uint8_t session_id[16];
  uint8_t sender_id[16];          // Must be initiator
  uint32_t settings_version;      // Increments on each change
  uint16_t settings_len;
  // Followed by serialized options (see lib/session/settings.c)
} acip_settings_sync_t;

// Settings acknowledgment (participant -> initiator)
PACKET_TYPE_ACIP_SETTINGS_ACK = 134,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];
  uint32_t settings_version;      // Version we applied
  uint8_t status;                 // 0=applied, 1=partial, 2=rejected
} acip_settings_ack_t;

// Host lost notification with NAT quality (participant -> ACDS)
// Combined packet: reports host loss AND provides candidacy for new host election
PACKET_TYPE_ACIP_HOST_LOST = 135,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];     // Who is reporting
  uint8_t last_host_id[16];       // The host that disconnected
  uint32_t disconnect_reason;     // 0=unknown, 1=timeout, 2=tcp_reset, 3=graceful

  // NAT quality for host election (same fields as NAT_QUALITY packet)
  uint8_t has_public_ip;
  uint8_t upnp_available;
  uint8_t upnp_mapped_port[2];
  uint8_t stun_nat_type;
  uint8_t lan_reachable;
  uint32_t stun_latency_ms;
  uint32_t upload_kbps;           // From bandwidth test before sending
  uint32_t download_kbps;
  uint16_t rtt_to_acds_ms;
  uint8_t jitter_ms;
  uint8_t packet_loss_pct;
  char public_address[64];
  uint16_t public_port;
  uint8_t ice_candidate_types;
} acip_host_lost_t;

// Host migration status (ACDS -> participant, "collecting candidates")
PACKET_TYPE_ACIP_HOST_MIGRATION = 136,
typedef struct {
  uint8_t session_id[16];
  uint8_t migration_id[16];       // Unique ID for this migration attempt
  uint8_t participant_id[16];     // Recipient's ID

  // Collection state
  uint8_t status;                 // 0=collecting, 1=electing, 2=complete, 3=failed
  uint8_t candidates_received;    // How many have reported so far
  uint8_t candidates_expected;    // Total known participants (excluding old host)
  uint16_t time_remaining_ms;     // Until collection window closes

  // If status == complete:
  uint8_t new_host_id[16];
  char new_host_address[64];
  uint16_t new_host_port;
  uint8_t you_are_host;           // 1 if recipient should start server

  // If status == failed:
  uint8_t failure_reason;         // 0=timeout_no_candidates, 1=only_one_left, 2=all_unreachable
} acip_host_migration_t;
```

### Modified Existing Packets

```c
// SESSION_JOINED response now includes host info
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];     // Your ID in this session
  uint8_t initiator_id[16];       // Who created the session (controls settings)

  // Host status
  uint8_t host_established;       // 0 = no host, negotiate; 1 = host exists

  // If host_established == 0:
  uint8_t peer_count;             // Other participants to negotiate with
  // Followed by: peer_count * participant_id[16]

  // If host_established == 1:
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;

  // STUN/TURN servers (always included)
  uint8_t stun_count;
  uint8_t turn_count;
  // Followed by: stun_server_t[], turn_server_t[]
} acip_session_joined_t;
```

## Settings Synchronization

### Mutable Settings

Settings can change mid-session (for future TUI support). The flow:

1. **Initiator changes a setting** (e.g., color mode)
2. **Initiator sends SETTINGS_SYNC** with incremented version
3. **Host receives and applies** (host may or may not be initiator)
4. **Host broadcasts to all participants**
5. **Each participant applies and sends SETTINGS_ACK**

### Serialization Format

Settings are serialized as a compact binary format:

```c
// lib/session/settings.h
typedef struct {
  uint32_t version;               // For conflict detection

  // Display settings
  int16_t width;                  // 0 = auto
  int16_t height;                 // 0 = auto
  uint8_t color_mode;             // terminal_color_mode_t
  uint8_t render_mode;            // render_mode_t
  uint8_t palette_type;           // palette_type_t
  char palette_custom[64];        // If palette_type == custom

  // Audio settings
  uint8_t audio_enabled;

  // Crypto settings
  uint8_t encryption_enabled;
  // Note: actual keys are NOT synced, only the "encryption required" flag

  // Reserved for future
  uint8_t reserved[32];
} session_settings_t;

// Serialize options_t -> session_settings_t -> binary
asciichat_error_t settings_serialize(const options_t *opts, uint8_t *buf, size_t *len);

// Deserialize binary -> session_settings_t -> merge into options_t
asciichat_error_t settings_deserialize(const uint8_t *buf, size_t len, options_t *opts);

// Compare versions, detect conflicts
bool settings_needs_update(uint32_t local_version, uint32_t remote_version);
```

## Code Refactoring Plan

### Phase 1: Abstract Server/Client Core

**Goal**: Extract reusable components from `src/server/` and `src/client/` into `lib/session/`.

#### New Library: lib/session/

```
lib/session/
├── host.h/c           # Server-side session management (extracted from src/server/)
│   - session_host_t struct
│   - session_host_create()
│   - session_host_start()
│   - session_host_add_participant()
│   - session_host_broadcast_frame()
│   - session_host_destroy()
│
├── participant.h/c    # Client-side session participation (extracted from src/client/)
│   - session_participant_t struct
│   - session_participant_create()
│   - session_participant_connect()
│   - session_participant_send_frame()
│   - session_participant_receive_frame()
│   - session_participant_destroy()
│
├── capture.h/c        # Unified webcam capture (shared by client/mirror/discovery)
│   - capture_context_t struct
│   - capture_start()
│   - capture_get_frame()
│   - capture_stop()
│
├── display.h/c        # Unified terminal display (shared by client/mirror/discovery)
│   - display_context_t struct
│   - display_init()
│   - display_render_frame()
│   - display_cleanup()
│
├── audio.h/c          # Unified audio handling
│   - audio_context_t struct
│   - audio_start_capture()
│   - audio_start_playback()
│   - audio_mix()
│   - audio_stop()
│
└── settings.h/c       # Session settings serialization
    - settings_serialize()
    - settings_deserialize()
    - settings_needs_update()
    - settings_apply()
```

#### Refactored src/ Structure

```
src/
├── main.c                    # Mode dispatcher (unchanged)
├── server/
│   └── main.c                # Thin wrapper around lib/session/host
├── client/
│   └── main.c                # Thin wrapper around lib/session/participant
├── mirror/
│   └── main.c                # Uses lib/session/capture + display (no network)
├── discovery-server/
│   └── main.c                # ACDS (extended for host tracking)
└── discovery/                # NEW: Discovery mode
    ├── main.c                # Entry point
    ├── nat.c                 # NAT quality detection (ICE + explicit checks)
    ├── negotiate.c           # Host negotiation logic
    └── session.c             # Combined host+participant management
```

### Phase 2: Discovery Mode Implementation

#### Entry Point: src/discovery/main.c

```c
int discovery_main(void) {
  const options_t *opts = options_get();

  // Determine if we're starting or joining
  bool is_initiator = (opts->session_string[0] == '\0');

  if (is_initiator) {
    return discovery_start_session();
  } else {
    return discovery_join_session(opts->session_string);
  }
}

static int discovery_start_session(void) {
  // 1. Connect to ACDS
  // 2. SESSION_CREATE → get session_id, participant_id, session_string
  // 3. Print session string for user to share
  // 4. Wait for PARTICIPANT_JOINED notification
  // 5. Start ICE gathering + NAT detection
  // 6. Exchange NAT_QUALITY with joiner
  // 7. Determine host (both sides run same algorithm)
  // 8. If we're host:
  //    a. Send HOST_ANNOUNCEMENT to ACDS
  //    b. Start server (lib/session/host)
  //    c. Join as participant (lib/session/participant)
  // 9. If they're host:
  //    a. Wait for HOST_DESIGNATED from ACDS
  //    b. Connect as participant
  // 10. Send SETTINGS_SYNC with our options
  // 11. Run until session ends
}

static int discovery_join_session(const char *session_string) {
  // 1. Connect to ACDS
  // 2. SESSION_JOIN → get response
  // 3. Check host_established flag:
  //
  //    If host_established == false (we're the first joiner):
  //      a. Start ICE gathering + NAT detection
  //      b. Exchange NAT_QUALITY with initiator
  //      c. Determine host
  //      d. If we're host: HOST_ANNOUNCEMENT, start server + participant
  //      e. If they're host: wait for HOST_DESIGNATED, connect
  //
  //    If host_established == true (host already exists):
  //      a. Connect directly to host_address:host_port
  //      b. No NAT negotiation needed
  //
  // 4. Receive SETTINGS_SYNC from initiator (via host)
  // 5. Apply settings
  // 6. Run until session ends
}
```

#### NAT Detection: src/discovery/nat.c

```c
typedef struct {
  // From explicit checks
  bool has_public_ip;             // STUN reflexive == local
  bool upnp_available;            // UPnP mapping succeeded
  uint16_t upnp_mapped_port;      // If upnp_available
  nat_type_t stun_nat_type;       // Classification
  bool lan_reachable;             // Same subnet as peer
  uint32_t stun_latency_ms;
  char public_address[64];
  uint16_t public_port;

  // Bandwidth measurements
  uint32_t upload_kbps;           // Upload speed (from ACDS test)
  uint32_t download_kbps;         // Download speed
  uint16_t rtt_to_acds_ms;        // Latency to ACDS
  uint8_t jitter_ms;              // Timing variance
  uint8_t packet_loss_pct;        // Loss percentage

  // From ICE gathering
  bool has_host_candidates;       // Local IP reachable
  bool has_srflx_candidates;      // STUN worked
  bool has_relay_candidates;      // TURN available
} nat_quality_t;

// Detect our NAT situation using both ICE and explicit checks
asciichat_error_t nat_detect_quality(nat_quality_t *out);

// Run bandwidth test against ACDS (64KB upload, measures receive rate)
asciichat_error_t nat_measure_bandwidth(nat_quality_t *out, int acds_socket);

// Compare two NAT qualities, return who should host
// Returns: -1 = we host, 0 = equal (initiator wins), 1 = they host
// Algorithm is deterministic - both sides get same result
int nat_compare_quality(const nat_quality_t *ours, const nat_quality_t *theirs,
                        bool we_are_initiator);
```

#### NAT Comparison Algorithm

```c
// Bandwidth override threshold: 10x difference can override NAT priority
#define BANDWIDTH_OVERRIDE_RATIO 10

int nat_compare_quality(const nat_quality_t *ours, const nat_quality_t *theirs,
                        bool we_are_initiator) {
  // First, compute NAT priority tiers (0=best, 4=worst)
  int our_tier = compute_nat_tier(ours);
  int their_tier = compute_nat_tier(theirs);

  // Check for bandwidth override: massive bandwidth advantage can override NAT tier
  // Example: 2 Mbps public IP loses to 50 Mbps UPnP
  if (ours->upload_kbps > 0 && theirs->upload_kbps > 0) {
    if (ours->upload_kbps >= theirs->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
      // We have 10x+ better upload - we should host regardless of NAT
      return -1;
    }
    if (theirs->upload_kbps >= ours->upload_kbps * BANDWIDTH_OVERRIDE_RATIO) {
      // They have 10x+ better upload - they should host regardless of NAT
      return 1;
    }
  }

  // No bandwidth override - use NAT tier comparison
  if (our_tier < their_tier) return -1;  // Lower tier = better
  if (our_tier > their_tier) return 1;

  // Same NAT tier - bandwidth is tiebreaker
  if (ours->upload_kbps > theirs->upload_kbps) return -1;
  if (ours->upload_kbps < theirs->upload_kbps) return 1;

  // Same bandwidth - latency is tiebreaker
  if (ours->rtt_to_acds_ms < theirs->rtt_to_acds_ms) return -1;
  if (ours->rtt_to_acds_ms > theirs->rtt_to_acds_ms) return 1;

  // Everything equal - initiator hosts
  return we_are_initiator ? -1 : 1;
}

static int compute_nat_tier(const nat_quality_t *q) {
  if (q->lan_reachable) return 0;           // LAN
  if (q->has_public_ip) return 1;           // Public IP
  if (q->upnp_available) return 2;          // UPnP
  if (q->stun_nat_type <= 2) return 3;      // STUN hole-punchable (open/full-cone/restricted)
  return 4;                                  // TURN relay required
}
```

### Phase 3: Options Refactoring

**Goal**: Define each option once, with groups and presets for sharing.

#### Current Problem

Options are defined multiple times across files:
- `lib/options/server.c` defines server options
- `lib/options/client.c` defines client options (some overlap)
- `lib/options/mirror.c` defines mirror options (mostly subset)
- `lib/options/discovery_server.c` defines ACDS options

#### Solution: Centralized Option Definitions

```c
// lib/options/definitions.h - Single source of truth

// Option groups (bit flags)
#define OPT_GROUP_DISPLAY     (1 << 0)   // width, height, color, palette
#define OPT_GROUP_NETWORK     (1 << 1)   // address, port
#define OPT_GROUP_WEBCAM      (1 << 2)   // webcam_index, flip, test_pattern
#define OPT_GROUP_AUDIO       (1 << 3)   // audio_enabled, mic, speakers
#define OPT_GROUP_CRYPTO      (1 << 4)   // encrypt, key, password
#define OPT_GROUP_DISCOVERY   (1 << 5)   // acds, webrtc, session settings
#define OPT_GROUP_SERVER      (1 << 6)   // max_clients, server-only
#define OPT_GROUP_CLIENT      (1 << 7)   // reconnect, client-only
#define OPT_GROUP_DEBUG       (1 << 8)   // verbose, log_file, log_level

// Mode presets (which groups each mode uses)
#define MODE_SERVER_GROUPS    (OPT_GROUP_DISPLAY | OPT_GROUP_NETWORK | OPT_GROUP_AUDIO | \
                               OPT_GROUP_CRYPTO | OPT_GROUP_DISCOVERY | OPT_GROUP_SERVER)
#define MODE_CLIENT_GROUPS    (OPT_GROUP_DISPLAY | OPT_GROUP_NETWORK | OPT_GROUP_WEBCAM | \
                               OPT_GROUP_AUDIO | OPT_GROUP_CRYPTO | OPT_GROUP_DISCOVERY | \
                               OPT_GROUP_CLIENT)
#define MODE_MIRROR_GROUPS    (OPT_GROUP_DISPLAY | OPT_GROUP_WEBCAM)
#define MODE_DISCOVERY_GROUPS (OPT_GROUP_DISPLAY | OPT_GROUP_WEBCAM | OPT_GROUP_AUDIO | \
                               OPT_GROUP_CRYPTO | OPT_GROUP_DISCOVERY)
#define MODE_ACDS_GROUPS      (OPT_GROUP_NETWORK | OPT_GROUP_CRYPTO)

// Option definition macro
#define DEFINE_OPTION(name, short_flag, long_flag, type, default_val, groups, help) \
  { .name = #name, .short_flag = short_flag, .long_flag = long_flag, \
    .type = type, .default_value = default_val, .groups = groups, .help = help }

// All options defined once
static const option_def_t ALL_OPTIONS[] = {
  // Display group
  DEFINE_OPTION(width,  'W', "width",  OPT_INT, "0", OPT_GROUP_DISPLAY,
                "Terminal width (0 = auto-detect)"),
  DEFINE_OPTION(height, 'H', "height", OPT_INT, "0", OPT_GROUP_DISPLAY,
                "Terminal height (0 = auto-detect)"),
  DEFINE_OPTION(color_mode, 0, "color-mode", OPT_ENUM, "auto", OPT_GROUP_DISPLAY,
                "Color mode: auto, mono, 16, 256, truecolor"),
  // ... etc

  // Network group
  DEFINE_OPTION(address, 'a', "address", OPT_STRING, "127.0.0.1", OPT_GROUP_NETWORK,
                "Bind/connect address"),
  DEFINE_OPTION(port, 'p', "port", OPT_INT, "27224", OPT_GROUP_NETWORK,
                "Port number"),
  // ... etc

  // Webcam group (client/mirror/discovery only)
  DEFINE_OPTION(webcam_index, 0, "webcam", OPT_INT, "0", OPT_GROUP_WEBCAM,
                "Webcam device index"),
  // ... etc
};
```

#### Mode-Specific Parsers Become Thin

```c
// lib/options/server.c - Now just a filter
asciichat_error_t parse_server_options(int argc, char **argv, options_t *opts) {
  return parse_options_with_groups(argc, argv, opts, MODE_SERVER_GROUPS);
}

// lib/options/discovery.c - NEW
asciichat_error_t parse_discovery_options(int argc, char **argv, options_t *opts) {
  return parse_options_with_groups(argc, argv, opts, MODE_DISCOVERY_GROUPS);
}
```

## Implementation TODOs

### Phase 1: Library Abstraction (Foundation)

- [ ] **TODO 1.1**: Create `lib/session/` directory structure
- [ ] **TODO 1.2**: Extract `session_host_t` from `src/server/` into `lib/session/host.c`
  - Identify server state that can be abstracted
  - Keep TCP accept loop, packet routing, frame broadcasting
  - Remove main() specific code
- [ ] **TODO 1.3**: Extract `session_participant_t` from `src/client/` into `lib/session/participant.c`
  - Identify client state that can be abstracted
  - Keep connection, frame send/receive, audio handling
  - Remove main() specific code
- [ ] **TODO 1.4**: Create `lib/session/capture.c` - unified webcam capture
  - Currently duplicated between `src/client/capture.c` and `src/mirror/main.c`
  - Single API for starting capture, getting frames, stopping
- [ ] **TODO 1.5**: Create `lib/session/display.c` - unified terminal display
  - Currently duplicated between `src/client/display.c` and `src/mirror/main.c`
  - Single API for rendering frames to terminal
- [ ] **TODO 1.6**: Create `lib/session/settings.c` - settings serialization
  - Serialize `options_t` subset to binary for network transmission
  - Deserialize and merge with local options
  - Version tracking for mid-session changes
- [ ] **TODO 1.7**: Update `src/server/main.c` to use `lib/session/host`
- [ ] **TODO 1.8**: Update `src/client/main.c` to use `lib/session/participant`
- [ ] **TODO 1.9**: Update `src/mirror/main.c` to use `lib/session/capture` + `display`
- [ ] **TODO 1.10**: Add tests for new library components

### Phase 2: Discovery Mode (Core Feature)

- [ ] **TODO 2.1**: Add `MODE_DISCOVERY` to `asciichat_mode_t` enum
- [ ] **TODO 2.2**: Create `src/discovery/` directory with `main.c`, `main.h`
- [ ] **TODO 2.3**: Register discovery mode in `src/main.c` mode table
- [ ] **TODO 2.4**: Implement NAT quality detection (`src/discovery/nat.c`)
  - Public IP detection via STUN (reflexive == local)
  - UPnP/NAT-PMP availability check
  - STUN NAT type classification
  - LAN reachability via mDNS/subnet check
  - ICE candidate type collection
- [ ] **TODO 2.4.1**: Implement bandwidth testing
  - ACDS upload test endpoint (client uploads 64KB, ACDS measures rate)
  - Include upload_kbps in SESSION_CREATED/SESSION_JOINED response
  - Add BANDWIDTH_TEST packet type for on-demand re-measurement
  - Track RTT, jitter, packet loss from ping/pong timing
- [ ] **TODO 2.4.2**: Implement protocol timing statistics
  - Track per-connection packet timing in stats struct
  - Calculate rolling average RTT, jitter, loss rate
  - Store historical stats per session string (optional, for returning users)
  - Log bandwidth decisions for debugging
- [ ] **TODO 2.5**: Add new ACIP packet types
  - `PACKET_TYPE_ACIP_NAT_QUALITY` (130)
  - `PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT` (131)
  - `PACKET_TYPE_ACIP_HOST_DESIGNATED` (132)
  - `PACKET_TYPE_ACIP_SETTINGS_SYNC` (133)
  - `PACKET_TYPE_ACIP_SETTINGS_ACK` (134)
  - `PACKET_TYPE_ACIP_HOST_LOST` (135)
  - `PACKET_TYPE_ACIP_HOST_MIGRATION` (136)
- [ ] **TODO 2.6**: Extend `acip_session_joined_t` with host info
  - `host_established` flag
  - `host_id`, `host_address`, `host_port` when established
  - `peer_count` + peer IDs when not established
- [ ] **TODO 2.7**: Update ACDS to track host per session
  - Store host info in SQLite after HOST_ANNOUNCEMENT
  - Include host info in SESSION_JOINED responses
  - Broadcast HOST_DESIGNATED to all participants
- [ ] **TODO 2.8**: Implement host negotiation logic (`src/discovery/negotiate.c`)
  - Deterministic comparison algorithm
  - Both sides compute same result
  - Initiator wins ties
- [ ] **TODO 2.9**: Implement discovery session flow (`src/discovery/session.c`)
  - Start session (create + wait for joiner)
  - Join session (check host_established, negotiate or connect)
  - Transition to host or participant role
- [ ] **TODO 2.10**: Implement dual-role for host (server + participant)
  - Host runs server AND participates in call
  - Uses `lib/session/host` + `lib/session/participant` together
- [ ] **TODO 2.11**: Implement mutable settings propagation
  - Initiator sends SETTINGS_SYNC on change
  - Host broadcasts to all participants
  - Participants apply and ACK
- [ ] **TODO 2.12**: Update CLI to make discovery mode the default
  - `ascii-chat` (no args) = start discovery session
  - `ascii-chat <session-string>` = join discovery session
  - `ascii-chat server` = explicit server mode (unchanged)
  - `ascii-chat client` = explicit client mode (unchanged)

### Phase 3: Options Refactoring (Quality of Life)

- [ ] **TODO 3.1**: Create `lib/options/definitions.h` with centralized option definitions
- [ ] **TODO 3.2**: Define option groups (display, network, webcam, audio, crypto, etc.)
- [ ] **TODO 3.3**: Define mode presets (which groups each mode uses)
- [ ] **TODO 3.4**: Implement `parse_options_with_groups()` that filters by group
- [ ] **TODO 3.5**: Refactor `lib/options/server.c` to use group filtering
- [ ] **TODO 3.6**: Refactor `lib/options/client.c` to use group filtering
- [ ] **TODO 3.7**: Refactor `lib/options/mirror.c` to use group filtering
- [ ] **TODO 3.8**: Refactor `lib/options/discovery_server.c` to use group filtering
- [ ] **TODO 3.9**: Create `lib/options/discovery.c` for discovery mode
- [ ] **TODO 3.10**: Update help output to show options by group
- [ ] **TODO 3.11**: Add tests for option group filtering

### Phase 4: Host Migration & Edge Cases

- [ ] **TODO 4.1**: Implement host migration (all-candidate election)
  - Participants detect host disconnect (TCP connection lost)
  - Each participant independently: STUN probe, UPnP check, bandwidth test
  - Send HOST_LOST + NAT_QUALITY to ACDS
  - ACDS collects ALL candidates during collection window
  - ACDS runs `nat_compare_quality()` on entire candidate pool
  - ACDS sends HOST_DESIGNATED to all (winner marked with `you_are_host=1`)
  - Winner starts server, others connect as clients
- [ ] **TODO 4.1.1**: Add migration state to ACDS session table
  - migration_id, migration_status, collection_start_time
  - candidates[] array with NAT_QUALITY for each
  - known_participant_count (to know when all have reported)
- [ ] **TODO 4.1.2**: Implement collection window logic
  - Start 5s timer on first HOST_LOST
  - Fast path: elect immediately when all known participants report
  - Timeout path: elect when timer expires AND candidates >= 2
  - Send HOST_MIGRATION status updates during collection (optional)
- [ ] **TODO 4.1.3**: Handle migration edge cases
  - Only one participant left → session ends
  - No candidates after timeout → session ends
  - New joiner during migration → added to candidates
  - Elected host fails to start → re-run election excluding them
  - Participant sends HOST_LOST twice → ignore duplicate
- [ ] **TODO 4.2**: Handle network changes mid-session
  - Detect IP change
  - Re-establish connection via fallback
- [ ] **TODO 4.3**: Add session persistence
  - Remember recent sessions
  - Quick rejoin on disconnect
- [ ] **TODO 4.4**: Add session security
  - Optional password for sessions
  - Key verification UI
- [ ] **TODO 4.5**: Documentation
  - Update README with discovery mode usage
  - Update man pages
  - Add examples

## File Changes Summary

### New Files

```
lib/session/
├── host.h
├── host.c
├── participant.h
├── participant.c
├── capture.h
├── capture.c
├── display.h
├── display.c
├── audio.h
├── audio.c
├── settings.h
└── settings.c

lib/options/
├── definitions.h
└── discovery.c

src/discovery/
├── main.h
├── main.c
├── nat.h
├── nat.c
├── negotiate.h
├── negotiate.c
├── session.h
└── session.c
```

### Modified Files

```
src/main.c                    # Add MODE_DISCOVERY to dispatcher
lib/options/options.h         # Add MODE_DISCOVERY enum, option groups
lib/options/options.c         # Handle discovery mode detection
lib/options/builder.c         # Support option group filtering
lib/options/server.c          # Use group filtering
lib/options/client.c          # Use group filtering
lib/options/mirror.c          # Use group filtering
lib/options/discovery_server.c            # Use group filtering
lib/network/acip/protocol.h   # Add new packet types (130-136, includes migration)
lib/network/acip/acds.h       # Add new message structures
src/discovery-server/         # Track host per session, broadcast HOST_DESIGNATED
src/server/main.c             # Use lib/session/host
src/client/main.c             # Use lib/session/participant
src/mirror/main.c             # Use lib/session/capture+display
CMakeLists.txt                # Add new source files
```

## Success Criteria

1. **User Experience**
   - `ascii-chat` prints a session string and waits
   - `ascii-chat swift-river-mountain` joins and call starts
   - No user action needed to determine host
   - 3rd+ participants just connect (no re-negotiation)

2. **Connection Quality**
   - Direct connections used when possible (public IP, UPnP)
   - Automatic fallback through STUN/TURN
   - Connection type logged for debugging
   - Host selection is optimal for network topology

3. **Settings Sync**
   - Initiator's display settings apply to all
   - Settings changes propagate mid-session
   - Participants see consistent experience
   - Invalid settings rejected gracefully

4. **Robustness**
   - Host disconnect triggers automatic migration
   - First two remaining participants renegotiate NAT
   - Third+ participants wait, then connect to new host
   - Call resumes automatically (no user action required)
   - Single participant or timeout → session ends cleanly

5. **Backward Compatibility**
   - `ascii-chat server` still works
   - `ascii-chat client` still works
   - `ascii-chat mirror` still works
   - Existing ACDS deployments unaffected

## Timeline Estimate

| Phase | Description | Complexity |
|-------|-------------|------------|
| 1 | Library Abstraction | Large - touches most of src/ |
| 2 | Discovery Mode | Large - new feature, protocol changes |
| 3 | Options Refactoring | Medium - mostly mechanical |
| 4 | Polish & Edge Cases | Medium - important for UX |

Recommended order: Phase 1 first (enables 2), then Phase 2 (core feature), Phase 3 can happen in parallel, Phase 4 last.
