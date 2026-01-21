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

2. **NETWORK_QUALITY Packet** (rich metadata)
   - Explicit UPnP/NAT-PMP availability (not visible in ICE)
   - STUN latency measurements
   - LAN reachability flags (same subnet detection)
   - **Bandwidth measurements** (upload speed matters for hosting)
   - Allows deterministic host selection before ICE completes

Both are used: ICE for actual connectivity, NETWORK_QUALITY for informed host selection.

### Bandwidth Detection: Background Media Measurement

**Key Design**: Bandwidth is measured from **actual media stream**, not synthetic tests. This ensures:
- **No connection blocking** - call starts immediately (<1 second)
- **Realistic metrics** - measured from real video frames
- **Continuous feedback** - network changes detected within 30-60s window
- **Fair comparison** - both upload and download measured equally

**How it works:**

Every video frame carries a timestamp. When received:
```
Frame received @ timestamp T with sequence N and size S
|-- Calculate RTT = (now - T)
|-- Track frame in rolling window (30-60 seconds)
|-- Count bytes received, frames received, RTT samples
|-- Accumulate loss (missing sequence numbers)
|-- Calculate jitter (variance of RTT)
```

After 30-60 seconds of media flow, each client **sends MEDIA_QUALITY_REPORT** to host with:
- Total bytes received (upload bandwidth = bytes * 8 / duration)
- Frames received (packet loss %)
- RTT min/max/avg, jitter (mean absolute deviation)
- Network quality assessment (0=poor, 1=fair, 2=good, 3=excellent)

**Host-side evaluation:**

Host collects MEDIA_QUALITY_REPORT from all participants every 30-60 seconds:
1. Scores each participant: `score = quality*1000 + (upload_kbps/1000)*10 + (100-rtt_ms) + initiator_bonus`
2. Elects best candidate as (new) host, second-best as backup
3. Broadcasts HOST_QUALITY_UPDATE to all participants
4. If current host is no longer best: triggers migration to new host

**Why upload speed matters:**
- Host sends video frames to ALL clients (upload-bound)
- Host must sustain N × frame_size Kbps (N = number of clients)
- Someone with 100Mbps down / 5Mbps up cannot host for 5+ people
- Network can be asymmetric: 1000 Mbps down / 10 Mbps up (fiber home)

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

### Initial Connection Timeline (Instant Media Start)

```
Alice starts session                              Bob joins
    |                                                 |
    |-- Connect to ACDS ------> [50-200ms]            |
    |-- SESSION_CREATE -------> [~10ms]               |
    |<- SESSION_CREATED -------- [~10ms]              |
    |   (no bandwidth test!)                          |
    |                                                 |
    |   "Session: swift-river-mountain"               |
    |   [Alice waits, shows session string]           |
    |                                                 |
    |                           [Bob enters string]   |
    |                           |-- Connect ACDS --> [50-200ms]
    |                           |-- SESSION_JOIN ---> [~10ms]
    |                           |<- SESSION_JOINED -- [~10ms]
    |                           |   host_established: false
    |                                                 |
    |   [Both start NAT probing in parallel]          |
    |   [NAT probing happens while exchanging signals] |
    |                                                 |
    |   [Both run NETWORK_QUALITY handshake]          |
    |<========== NETWORK_QUALITY exchange =========> [~100ms]
    |   (exchange NAT type, UPnP capability, etc.)    |
    |   (NOT bandwidth yet!)                          |
    |                                                 |
    |   [Both compute winner - instant]               |
    |                                                 |
    |-- HOST_ANNOUNCEMENT ----> [~10ms] (if winner)   |
    |<- HOST_DESIGNATED ------- [~10ms]               |
    |                                                 |
    |   [Winner starts server] [~50ms]                |
    |   [Loser connects]       [~50ms]                |
    |                                                 |
    |<=============== MEDIA STARTS ==================>|
    |                                                 |
    | [Background: bandwidth measurement from media]  |
    | [After 30 seconds...]                           |
    |                                                 |
    | [Each sends MEDIA_QUALITY_REPORT to host]       |
    | [Host evaluates quality and broadcasts]         |
    | [HOST_QUALITY_UPDATE sent to all participants]  |
    | [Backup host confirmed]                         |

TOTAL: ~0.8-1.2 seconds from Bob pressing enter to call starting
       + 30-60 seconds for bandwidth measurements to accumulate
```

### Migration Timeline (Instant Failover with Pre-Elected Backup)

```
T=0s:   Call in progress
        Host (Alice) broadcasts HOST_QUALITY_UPDATE every 30-60s:
        "I'm host, Bob is backup. Bob's address: 192.168.1.101:27224"
        [All participants store backup info locally]

T=90s:  Alice (host) crashes
        TCP connection drops
            X

T=90.1s: Bob detects TCP connection lost
        "I stored backup host info: Bob = 192.168.1.101:27224"
        "That's me! I should become host."
        |-- Start server immediately on 192.168.1.101:27224
        |   [~50ms to start server]

T=90.1s: Carol detects TCP connection lost
        "I stored backup host info: Bob = 192.168.1.101:27224"
        |-- Connect to Bob at 192.168.1.101:27224
        |   [~100ms to establish connection]

T=90.1s: Dave detects TCP connection lost
        "Connecting to backup at 192.168.1.101:27224"
        |-- Connect to Bob
        |   [~100ms]

T=90.2s: [Optional: Send HOST_LOST notification to ACDS]
        (informational only, no election needed)

T=90.3s: Media resumes
        [All participants successfully connected to new host Bob]
        [No ACDS query, no negotiation, no bandwidth test delay]

TOTAL: ~200-500ms from detection to call resume
       (host detection immediate, participant reconnect ~100ms)
```

**Key insight**: Future host is PRE-ELECTED every 30-60 seconds via HOST_QUALITY_UPDATE.
When current host dies, everyone already knows where to connect. No ACDS election needed.

### Timeout Constants

```c
// Connection timeouts
#define ACDS_CONNECT_TIMEOUT_MS      5000    // TCP connect to ACDS
#define ACDS_HANDSHAKE_TIMEOUT_MS    3000    // Complete handshake after connect

// NAT probing timeouts (no bandwidth test - measured from media)
#define STUN_PROBE_TIMEOUT_MS        2000    // STUN binding request
#define UPNP_PROBE_TIMEOUT_MS        3000    // UPnP/NAT-PMP discovery + mapping
#define NETWORK_QUALITY_EXCHANGE_MS  2000    // Peer NETWORK_QUALITY round-trip

// Session timeouts
#define SESSION_JOIN_TIMEOUT_MS      10000   // Max time to join after SESSION_JOIN sent
#define HOST_NEGOTIATION_TIMEOUT_MS  5000    // Max time for initial 2-party negotiation

// Media quality measurement
#define MEDIA_QUALITY_WINDOW_MS      30000   // Measurement window (30 seconds)
#define MEDIA_QUALITY_REPORT_TIMEOUT_MS 5000 // Wait for quality reports from all
#define HOST_ELECTION_INTERVAL_MS    30000   // Re-evaluate host every 30 seconds

// Migration timeouts
#define MIGRATION_DETECTION_TIMEOUT_MS 1000  // How long to wait before declaring host dead
#define BACKUP_HOST_FALLBACK_TIMEOUT_MS 3000 // Try backup connection for 3 seconds
#define HOST_STARTUP_TIMEOUT_MS      1000    // New host must accept connections within 1s

// Keepalive / disconnect detection
#define KEEPALIVE_INTERVAL_MS        5000    // Ping frequency
#define KEEPALIVE_TIMEOUT_MS         15000   // Miss 3 pings = disconnect
#define TCP_USER_TIMEOUT_MS          30000   // TCP-level timeout (RFC 5482)
```

### User-Visible Latency Summary

| Scenario | Best Case | Typical | Worst Case |
|----------|-----------|---------|------------|
| Start session (to string shown) | 200ms | 500ms | 1.5s |
| Join session (to media starts) | 800ms | 1.0s | 2.0s |
| Bandwidth measurement (quality accumulated) | 30s | 45s | 60s |
| Host re-election (after quality measurement) | Instant | 1s | 5s |
| Host migration (to call resumes) | 200ms | 500ms | 1.5s |
| Disconnect detection | 0ms (RST) | 5s | 30s (timeout) |

**Key improvement**: Call starts in ~1 second without bandwidth test blocking.
Bandwidth measurements happen in background while media flows.
Migration detection and failover is ~500ms using pre-stored backup address.

### Optimizations for Speed

1. **No blocking bandwidth test** - Measured from actual media stream
2. **Parallel NAT probing** - STUN, UPnP run concurrently during handshake
3. **Instant media start** - Call begins immediately after host negotiation
4. **Optimistic UI** - Show media while quality measurements accumulate
5. **Pre-elected backup** - Host_QUALITY_UPDATE sent every 30-60s with backup address
6. **Instant failover** - Backup address already stored locally, no ACDS query on failure
7. **Cached NAT info** - Remember NAT type from previous sessions (valid ~5 min)

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
  |<======== NETWORK_QUALITY exchange (via ACDS relay) ===========>|
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

When the host disconnects, **remaining participants immediately know who the new host is** (pre-elected 5 minutes ago). They simply connect to the stored address/port. **ACDS is notified for bookkeeping only**.

#### Migration Flow (Instant Failover - No Re-Election Needed!)

```
Host (Bob, disconnects)     Alice      Carol      Dave       ACDS
         X                    |          |          |         |
         |                    |          |          |         |
    [Detect TCP RST/timeout]  |          |          |         |
         |                    |          |          |         |
         |   [All detect TCP connection lost]         |
         |                    |          |          |         |
         |-- HOST_LOST ------->--------- ----------->-------> [notification only]
         |   (just "Bob is gone")                    |
         |                    |          |          |         |
         | [Carol was pre-elected as future host 5 minutes ago]
         | [All participants stored: Carol's address, port, connection type]
         |                    |          |          |         |
         |<==== Carol connects to new participants if NAT requires P2P ====>|
         |      (e.g., if Carol behind symmetric NAT needing STUN/TURN)    |
         |      [This P2P is only for NAT traversal, NOT for metrics]      |
         |                    |          |          |         |
         |   [Carol starts server + joins as participant]      |
         |   [Alice and Dave connect to Carol using stored info]           |
         |                    |          |          |         |
         |<========== Video call resumes =====================>|
         |                    |          |          |         |
         |<-- HOST_DESIGNATED --------- ----------->-------> [confirmation]
         |   (ACDS confirms, for session bookkeeping)         |
```

**Key Insight**: Future host already elected during last 5-minute ring consensus!

Unlike the old ACDS-centric model, migration is now **pre-determined**:

1. **During normal call (every 5 minutes or on new join)**:
   - Participants send NETWORK_QUALITY to host via existing connection
   - Host collects metrics and computes future host using `nat_compare_quality()`
   - Host broadcasts FUTURE_HOST_ELECTED with:
     - **Who** will be the future host
     - **Where** to connect (address, port)
     - **How** to connect (DIRECT_PUBLIC, UPNP, STUN, TURN)
   - All participants store this information locally

2. **When current host dies**:
   - Participants already know the future host (stored from last broadcast)
   - No election needed (already determined!)
   - No metrics exchange needed (already fresh from 5-minute probes!)
   - Immediately attempt connection using stored address/port
   - If NAT traversal needed: establish P2P connection to new host (for NAT, not metrics)
   - Call resumes within ~500ms (just connection establishment time)

3. **ACDS MUST stay in sync with current host (critical for new joiners)**:
   - New participants call SESSION_JOIN and ask ACDS "who is the host?"
   - ACDS responds with current host address/port in SESSION_JOINED
   - **Therefore**: ACDS must be updated with current host info so new participants can join
   - Send HOST_ANNOUNCEMENT when becoming host (tells ACDS who's hosting now)
   - ACDS stores current_host_id, current_host_address, current_host_port in session state
   - Can detect if migration stalls (no update for 30+ seconds) and cleanup/timeout
   - Can detect network splits (conflicting HOST_ANNOUNCEMENT claims)

**Why pre-election is better**:
- **No stale data**: Metrics refreshed every 5 minutes (before needed)
- **Instant failover**: No election delay when host dies (<500ms)
- **No ACDS election delay**: Existing participants already know the plan (pre-elected 5min ago)
- **Deterministic**: Same algorithm + same inputs = same result for all
- **Minimal P2P**: Only needed for NAT traversal if required, not for metrics
- **Simple**: NETWORK_QUALITY never exchanged during migration window
- **ACDS still needed**: But only for new joiners (who is the current host?), not for failover decisions

#### Network Statistics Exchange (NETWORK_QUALITY - Proactive Only)

Participants exchange **live** network metrics with the HOST (not peer-to-peer), every 5 minutes or on new join:

```
Participant A              Host (B)                Participant C
    |                          |                       |
    |-- NETWORK_QUALITY ------->|                       |
    |   {upload: 50 Mbps,       |                       |
    |    nat: public_ip, ...}   |                       |
    |                          |                       |
    |<-- NETWORK_QUALITY --------|                       |
    |   {upload: 25 Mbps,       |                       |
    |    nat: upnp, ...}        |                       |
    |                          |                       |
    |                          |-- NETWORK_QUALITY ----->|
    |                          |   {upload: 25 Mbps,   |
    |                          |    nat: upnp, ...}    |
    |                          |                       |
    |                          |<-- NETWORK_QUALITY -----
    |                          |   {upload: 10 Mbps,   |
    |                          |    nat: stun, ...}    |
    |                          |                       |
    | [B collects all metrics, elects future host]     |
    |<-- FUTURE_HOST_ELECTED ----|-- FUTURE_HOST_ELECTED -->|
    |   {future_host: A,        |   {future_host: A,      |
    |    address: ..., port: ...}    address: ..., port: ...}
    |                          |                       |
    | [A, B, C all store: A is future host]             |
    | [Call continues normally]                         |
    |                          |                       |
    | [If B dies within 5 min, everyone already knows A is next host]
```

**Key point**: NETWORK_QUALITY is exchanged through the host during normal operation, NOT peer-to-peer. When migration happens, participants don't need to exchange metrics - they already have fresh data from the last ring consensus!

#### HOST_LOST Packet (Simplified)

HOST_LOST now carries **only notification** data, not NAT metrics:

```c
PACKET_TYPE_ACIP_HOST_LOST = 135,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];      // Who is reporting
  uint8_t last_host_id[16];        // The host that disconnected
  uint32_t disconnect_reason;      // 0=unknown, 1=timeout, 2=tcp_reset, 3=graceful
  uint64_t disconnect_time_ms;     // When we detected it (for ACDS bookkeeping)
} acip_host_lost_t;
```

**Note**: NAT quality data moved to **separate NETWORK_QUALITY packet** for ongoing measurements, not bundled with loss notification.

#### NETWORK_QUALITY Packet (New)

Participants exchange live network metrics **continuously** (useful for all phases):

```c
PACKET_TYPE_ACIP_NETWORK_QUALITY = 137,  // NEW packet type
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];

  // NAT detection results (live, current measurements)
  uint8_t has_public_ip;
  uint8_t upnp_available;
  uint8_t upnp_mapped_port[2];
  uint8_t stun_nat_type;
  uint8_t lan_reachable;
  uint32_t stun_latency_ms;

  // Bandwidth measurements (live)
  uint32_t upload_kbps;
  uint32_t download_kbps;
  uint16_t rtt_to_peer_ms;         // Latency to measurement reference
  uint8_t jitter_ms;
  uint8_t packet_loss_pct;

  // Connection info
  char public_address[64];
  uint16_t public_port;

  // ICE candidate summary
  uint8_t ice_candidate_types;
} acip_network_stats_t;
```

#### Migration Timeline (Instant Failover from Pre-Election)

```
Last ring consensus (5 minutes ago)   All know: Carol will host at 192.168.1.100:27224
        ^                             Store this information locally
        |
        v [5 minutes later]
Host crashes
    X                               All participants              ACDS
    |                                   |                         |
    |                                   |                         |
    |   [Detect disconnect]             |                         |
    |   - TCP RST: immediate            |                         |
    |   - Timeout: up to 30s            |                         |
    |                                   |                         |
    |-- HOST_LOST ---------->----------->----------> [notification]
    |   (just "Bob is gone")            |                         |
    |                                   |                         |
    | [Carol immediately knows she's the future host]            |
    | [All know Carol's address:port (already stored!)]          |
    |                                   |                         |
    |   [Carol starts server]  [~100ms] |                         |
    |                                   |                         |
    |   [Carol sends HOST_ANNOUNCEMENT]---------> [ACDS updates to Carol as current host]
    |   [tells ACDS: Carol is hosting at ...]                    (critical for new joiners!)
    |                                   |                         |
    |   [Exchange WebRTC SDP over TCP] [~50-100ms]               |
    |<========== SDP + ICE candidates ===========>                |
    |                                   |                         |
    |   [WebRTC P2P negotiation]        |                         |
    |<========== ICE candidates ===========>                      |
    |                                   |                         |
    |<======== CALL RESUMES (P2P media) ==================>       |
    |                                   |                         |
    |                                   <-- HOST_DESIGNATED ----- [confirmation to all participants]
    |                                   (ACDS confirms, for new joiners during migration)

TOTAL: ~500ms (existing participants use pre-stored address, no ACDS election!)
       + disconnect detection time (0-30s depending on failure mode)

**Critical messages during migration**:
1. **HOST_LOST** (to ACDS): Notification that old host died
2. **HOST_ANNOUNCEMENT** (to ACDS): New host announces address/port (allows new joiners to find them)
3. **SDP/ICE exchange** (direct TCP, no ACDS): Direct peer-to-peer WebRTC signaling

**Key optimization**: Existing participants don't need ACDS for failover - they use pre-stored future host
address. New participants joining during migration still need ACDS to find the new host.
```

**Key difference**: No election delay. Future host was pre-elected 5 minutes ago. Migration is just
connecting to known address + SDP exchange (500ms total vs 5+ seconds with election). **But ACDS must
be kept in sync with current host so new joiners can find it**.

#### Migration State Machine (Participant-Side)

```
CALL_ACTIVE (connected to host)
    |
    v [TCP connection lost]
MIGRATION_DETECTED
    |-- We know the future host from last FUTURE_HOST_ELECTED broadcast
    |-- We have stored address, port, connection type
    |
    v [immediately]
IF (I'm the future host):
    STARTING_SERVER
    |-- Start hosting with my stored connection info
    |-- Register as HOST_ANNOUNCED with ACDS (optional)
    |
    v [~100ms server startup]
    CALL_ACTIVE (as new host)

ELSE:
    CONNECTING_TO_NEW_HOST
    |-- Use stored future_host_address:future_host_port
    |-- Establish connection (may need P2P for NAT traversal)
    |-- time_until_connected (~100-200ms)
    |
    v [connection established]
    CALL_ACTIVE (resumed as participant)
```

**Key difference**: No DETECTING, EXCHANGING, or ELECTING states. We already know who the host is!

#### ACDS Role (NOT for election, BUT for new joiner discovery)

**What ACDS does NOT do**:
- ❌ Does not run election (participants know pre-elected future host!)
- ❌ Does not collect NAT quality (already done 5 minutes ago!)
- ❌ Does not make any failover decisions (existing participants don't need it!)

**What ACDS MUST do**:
- ✅ Keep current_host in session state (updated by HOST_ANNOUNCEMENT)
- ✅ Return current host address/port to new joiners in SESSION_JOINED response
- ✅ Track migration state for new joiners joining during failover window
- ✅ Detect stalled migrations (no update for 30+ seconds = cleanup)

**Why ACDS is still critical**:
When someone calls `SESSION_JOIN("swift-river-mountain")`, ACDS must answer:
- "The current host is Carol at 192.168.1.100:27224"
- Otherwise new participants can't find the session!

#### ACDS Migration Tracking (For New Joiners + Stall Detection)

ACDS tracks migration state to handle new participants joining during failover:

```c
// ACDS session state (SQLite)
current_host_id:  uint8_t[16]      // Who is hosting RIGHT NOW (updated by HOST_ANNOUNCEMENT)
current_host_address:  char[64]    // Host's address (for new joiners to connect)
current_host_port:  uint16_t       // Host's port (for new joiners to connect)

// Migration tracking
in_migration:  true/false          // Is a migration in progress?
migration_start_ms:  uint64_t       // When HOST_LOST was received
migration_participants:  list       // Who reported HOST_LOST
```

ACDS uses this for:
- **New joiner discovery**: Tell them who the current host is (SESSION_JOINED response)
- **Detecting stalled migrations**: If in_migration=1 for >30s, migration failed → cleanup
- **Handling new joiners during migration**: Tell them to wait for HOST_DESIGNATED
- **Network split detection**: Multiple conflicting HOST_ANNOUNCEMENT claims
- **Statistics/logs**: Track migration performance

#### Edge Cases

- **Only one participant left**: Session ends immediately (can't have a call alone)
- **Multiple simultaneous disconnects**: Multiple participants may start new migrations
  - All compute same result, only one succeeds in becoming host
- **New joiner during migration**:
  - Doesn't participate in exchange (doesn't have disconnect detection trigger)
  - Waits for HOST_DESIGNATED confirmation
  - Connects to elected host when ready
- **Elected participant can't reach the others**: Uses fallback (TURN relay)
  - Still becomes host, even if temporarily unreachable
  - Participants retry connection with fallback mechanisms
- **Network partition**: Participants on different sides may elect different hosts
  - Both may try to become host (competing servers)
  - Resolved when partition heals (one session wins, others merge or end)
  - Client connections fail → reconnect logic retries with other host

## ACIP Protocol Extensions

### New Packet Types

```c
// Network quality metrics (participant <-> participant or participant -> ACDS)
// Used for:
// 1. Initial negotiation: peers exchange to determine host
// 2. Migration: peers exchange live metrics for re-negotiation
// 3. Continuous monitoring: ongoing quality tracking (future)
PACKET_TYPE_ACIP_NETWORK_QUALITY = 130,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];

  // NAT detection results (current measurements)
  uint8_t has_public_ip;          // STUN reflexive == local IP
  uint8_t upnp_available;         // UPnP/NAT-PMP port mapping works
  uint8_t upnp_mapped_port[2];    // Port we mapped (network byte order)
  uint8_t stun_nat_type;          // 0=open, 1=full-cone, 2=restricted, 3=port-restricted, 4=symmetric
  uint8_t lan_reachable;          // Same subnet (mDNS/ARP)
  uint32_t stun_latency_ms;       // RTT to STUN server

  // Bandwidth measurements
  uint32_t upload_kbps;           // Upload speed in Kbps
  uint32_t download_kbps;         // Download speed in Kbps
  uint16_t rtt_ms;                // Round-trip latency (to ACDS, peer, or reference point)
  uint8_t jitter_ms;              // Packet timing variance (0-255ms)
  uint8_t packet_loss_pct;        // Packet loss percentage (0-100)

  // Connection info
  char public_address[64];        // Our public IP (if has_public_ip or upnp)
  uint16_t public_port;           // Our public port

  // ICE candidate summary
  uint8_t ice_candidate_types;    // Bitmask: 1=host, 2=srflx, 4=relay

  // Timestamp (for staleness detection)
  uint64_t measurement_time_ms;   // When this measurement was taken
} acip_network_quality_t;

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

// Host lost notification (participant -> ACDS)
// Lightweight notification that host disconnected
PACKET_TYPE_ACIP_HOST_LOST = 135,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];     // Who is reporting
  uint8_t last_host_id[16];       // The host that disconnected
  uint32_t disconnect_reason;     // 0=unknown, 1=timeout, 2=tcp_reset, 3=graceful
  uint64_t disconnect_time_ms;    // When detected (for ACDS bookkeeping)
} acip_host_lost_t;

// Media quality report (participant -> host)
// Client reports measured bandwidth, RTT, jitter, packet loss from media stream
// Sent every 30-60 seconds after call starts (measured from actual frames received)
PACKET_TYPE_ACIP_MEDIA_QUALITY_REPORT = 146,
typedef struct {
  uint8_t session_id[16];
  uint8_t participant_id[16];

  // Measurement window
  uint32_t window_duration_ms;     // How long we measured (30000-60000)
  uint32_t frames_expected;         // Expected frame count in window (frame_rate * duration)
  uint32_t frames_received;         // Actually received

  // Upload metrics (what we received from host/peers)
  uint32_t bytes_received;          // Total bytes in window
  uint32_t upload_kbps;             // (bytes_received * 8) / window_duration_ms
  uint8_t packet_loss_pct;          // (frames_expected - frames_received) / frames_expected

  // RTT metrics (measured from frame timestamps)
  uint16_t rtt_min_ms;              // Minimum RTT in window
  uint16_t rtt_max_ms;              // Maximum RTT in window
  uint16_t rtt_avg_ms;              // Average RTT
  uint8_t jitter_ms;                // Peak-to-peak jitter (max_rtt - min_rtt)
  uint8_t jitter_mad_ms;            // Mean absolute deviation jitter

  // Network condition assessment
  uint8_t network_quality;          // 0=poor, 1=fair, 2=good, 3=excellent

  uint8_t reserved[32];
} acip_media_quality_report_t;

// Host quality update (host -> all participants)
// Host announces current host, backup host, and their connection info
// Sent every 30-60 seconds with updated metrics and host election
PACKET_TYPE_ACIP_HOST_QUALITY_UPDATE = 147,
typedef struct {
  uint8_t session_id[16];

  // Current host
  uint8_t host_id[16];
  char host_address[64];
  uint16_t host_port;

  // Backup host (for automatic failover if current host dies)
  uint8_t backup_host_id[16];
  char backup_host_address[64];
  uint16_t backup_host_port;

  // Metrics (for transparency and debugging)
  uint32_t host_upload_kbps;        // Host's measured upload capacity
  uint32_t backup_upload_kbps;      // Backup's measured upload capacity
  uint16_t host_avg_rtt_ms;         // Host's RTT to participants
  uint16_t backup_avg_rtt_ms;       // Backup's RTT to participants

  // Timing
  uint64_t elected_at_ns;           // When this decision was made
  uint32_t next_reeval_in_ms;       // When next re-election will happen

  // Context
  uint8_t participant_count;        // How many are in the call
  uint8_t worst_link_quality;       // Worst quality link (0-3 scale, limiting factor)

  uint8_t reserved[32];
} acip_host_quality_update_t;

// WebRTC SDP and ICE are handled by existing packet types:
// - PACKET_TYPE_ACIP_WEBRTC_SDP (110): Bidirectional SDP exchange
// - PACKET_TYPE_ACIP_WEBRTC_ICE (111): Bidirectional ICE candidates
// These were originally designed for ACDS-relayed signaling but are reused
// for direct P2P signaling during failover (bypass ACDS, go directly to new host)
```

**Protocol Summary**:
- **NETWORK_QUALITY** (130): NAT type/UPnP/ICE info for initial host negotiation
  - Sent during first two participants handshake to determine who hosts
  - Light-weight (no bandwidth data, only connectivity info)
- **HOST_ANNOUNCEMENT** (131): Winner announces they're hosting
  - Sent to ACDS and other participants after negotiation
- **HOST_DESIGNATED** (132): ACDS confirms to all
  - Broadcast confirmation of elected host
- **SETTINGS_SYNC** (133): Initiator changes settings
  - Session-wide settings synchronization
- **SETTINGS_ACK** (134): Participants acknowledge settings
  - Confirmation that settings were applied
- **HOST_LOST** (135): Notification that host disconnected (lightweight)
  - Informational only, sent to ACDS for logging
  - Does NOT trigger election (backup already known)
- **MEDIA_QUALITY_REPORT** (146): Participant reports measured bandwidth/RTT/jitter
  - Sent to host every 30-60 seconds
  - Measured from actual media frames (not synthetic test)
  - Includes: upload Kbps, RTT min/max/avg, jitter, packet loss %, network quality
  - Host collects these reports from all participants
- **HOST_QUALITY_UPDATE** (147): Host announces host/backup and metrics
  - Sent by host to all participants every 30-60 seconds
  - Includes: current host address, backup host address, election metrics, re-eval interval
  - All participants store backup address locally for instant failover
  - Replaces FUTURE_HOST_ELECTED with richer data and more frequent updates
- **WEBRTC_SDP** (110): Bidirectional WebRTC SDP exchange (offer/answer)
  - Originally designed for ACDS-relayed signaling
  - Reused for direct P2P signaling during failover
  - Participant → new host, or bidirectional during failover negotiation
- **WEBRTC_ICE** (111): Bidirectional ICE candidate exchange
  - Originally designed for ACDS-relayed signaling
  - Reused for direct P2P signaling during failover
  - NAT traversal candidates for P2P connection

### Instant Failover with Pre-Stored Backup Address

**Key Insight**: Backup host address is broadcast via HOST_QUALITY_UPDATE every 30-60 seconds.
When current host dies, participants immediately know where backup is listening (no ACDS query needed).

**Failover Flow**:
1. Host broadcasts HOST_QUALITY_UPDATE with backup address/port stored locally by all participants
2. If current host dies (TCP connection drops), all participants instantly know:
   - Who the backup is: backup_host_id from stored HOST_QUALITY_UPDATE
   - Where to connect: backup_host_address:backup_host_port (already stored)
3. Backup participant (who was elected) immediately starts listening on that address/port
4. Other participants connect directly to backup using stored address
5. Once connected, WebRTC SDP/ICE exchange negotiates P2P media
6. Media resumes within ~200-500ms (no ACDS lookup, no election delay)

**Advantages**:
- **No ACDS dependency during failover**: Everyone already knows backup address
- **Instant failover**: ~200-500ms from detection to call resume
- **Deterministic**: No election needed, backup was pre-chosen and stored
- **No network partition**: Pre-election means same result all sides
- **Scales better**: No central ACDS election bottleneck

**Failover Timeline**:
```
Host dies (T=0ms)
  ↓
Participants detect TCP disconnect (T=0-100ms)
  ↓
Backup starts listening on stored address (T=50ms)
  ↓
Others connect to backup (T=100-200ms)
  ↓
WebRTC negotiation (T=200-300ms)
  ↓
Media resumes (T=300-500ms)
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
  // 6. Exchange NETWORK_QUALITY with joiner
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
  //      b. Exchange NETWORK_QUALITY with initiator
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
- [ ] **TODO 2.4.1**: Implement per-frame RTT and quality tracking in participant code
  - Track frame sequence numbers to detect loss (missing sequence = lost frame)
  - Extract timestamp from each frame, calculate RTT = now - frame_timestamp
  - Accumulate bytes_received, frames_received per 30-60 second window
  - Calculate bandwidth: (bytes_received * 8) / window_duration_ms
  - Collect RTT samples, compute min/max/avg/jitter
  - Compute network quality assessment (0-3 scale based on loss/jitter/rtt)
- [ ] **TODO 2.4.2**: Implement background quality reporting (every 30-60s)
  - Create MEDIA_QUALITY_REPORT after each measurement window
  - Send to host with accumulated bandwidth/RTT/jitter metrics
  - Reset measurement window, start accumulating again
  - Do NOT block call, send in background
  - Handle gracefully if host doesn't receive report
- [ ] **TODO 2.5**: Add new ACIP packet types (most already exist, just add two new ones)
  - `PACKET_TYPE_ACIP_NETWORK_QUALITY` (130) - ✓ already exists (NAT info for negotiation)
  - `PACKET_TYPE_ACIP_HOST_ANNOUNCEMENT` (131) - ✓ already exists
  - `PACKET_TYPE_ACIP_HOST_DESIGNATED` (132) - ✓ already exists
  - `PACKET_TYPE_ACIP_SETTINGS_SYNC` (133) - ✓ already exists
  - `PACKET_TYPE_ACIP_SETTINGS_ACK` (134) - ✓ already exists
  - `PACKET_TYPE_ACIP_HOST_LOST` (135) - ✓ already exists
  - `PACKET_TYPE_ACIP_MEDIA_QUALITY_REPORT` (146) - NEW: participant → host, bandwidth/RTT/jitter
  - `PACKET_TYPE_ACIP_HOST_QUALITY_UPDATE` (147) - NEW: host → all, announce host/backup with metrics
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
- [ ] **TODO 2.12**: Implement host-side quality evaluation and election
  - Collect MEDIA_QUALITY_REPORT from all participants every 30-60s
  - Score each participant: quality*1000 + bandwidth*10 + (100-rtt) + initiator_bonus
  - Elect best as (new) host, second-best as backup
  - If current host is no longer best: trigger host migration
  - If host stays same but backup changed: just send new HOST_QUALITY_UPDATE
  - Broadcast HOST_QUALITY_UPDATE with metrics, current host, backup host, addresses
- [ ] **TODO 2.13**: Implement CLIENT-side backup address storage
  - On receiving HOST_QUALITY_UPDATE: store backup_host_address, backup_host_port locally
  - Use stored address immediately when TCP disconnect detected (no ACDS query)
- [ ] **TODO 2.14**: Update CLI to make discovery mode the default
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

### Phase 4: Instant Failover & Polish

- [ ] **TODO 4.1**: Implement instant failover using pre-stored backup address
  - Detect host disconnect (TCP RST or timeout)
  - Check if we are the backup (compare our ID with backup_host_id from HOST_QUALITY_UPDATE)
  - If we're backup: start server on stored backup_host_address:backup_host_port
  - If we're not backup: connect to stored backup address using TCP socket
  - No ACDS query, no election, no NETWORK_QUALITY exchange needed
  - Media resumes within ~500ms total
- [ ] **TODO 4.2**: Send HOST_LOST notification to ACDS (optional, informational only)
  - Notify ACDS that host died (for logging/analytics)
  - Include participant_id, host_id, disconnect_reason, timestamp
  - Does NOT require ACDS response or action
  - ACDS can update in_migration flag for bookkeeping
- [ ] **TODO 4.3**: Handle edge cases in failover
  - Only one participant left → call ends (session becomes invalid)
  - Backup can't listen on stored address → log error, try TURN relay
  - New joiner during migration → use current host from ACDS (may be transitional)
  - Network partition → both sides may try to become host
    - Resolution: first one to establish connection wins
    - Loser detects conflict, reconnects to winner
  - Participant offline during migration → skipped (no impact on others)
- [ ] **TODO 4.4**: Implement WebRTC P2P SDP/ICE exchange for failover
  - After backup starts listening and others connect to it
  - Exchange SDP offers/answers directly over TCP (not via ACDS)
  - Exchange ICE candidates for NAT traversal
  - Establish WebRTC connection for actual media flow
  - Media can be P2P (if NAT allows) or through TURN relay (fallback)
  - Reduces host CPU load once P2P established (no media relay needed)
- [ ] **TODO 4.5**: Add session persistence (nice-to-have)
  - Remember recent sessions and their bandwidth metrics
  - Allow quick rejoin: `ascii-chat swift-river-mountain` reconnects instantly
  - Use cached bandwidth data on rejoin
  - Invalidate cache after session expires (24 hours)
- [ ] **TODO 4.6**: Add session security features (nice-to-have)
  - Optional password protection for sessions
  - Session-specific credentials or tokens
  - Key fingerprint verification UI
  - Trust-on-first-use (TOFU) for session participants
- [ ] **TODO 4.7**: Comprehensive testing
  - Unit tests: quality scoring algorithm, backup election logic
  - Integration test: start session → join → media flows → measurement window → re-election
  - Integration test: host dies → backup detected → media resumes on backup
  - Load test: 5+ participants, verify all get correct bandwidth measurements
  - Edge case: new joiner while migration in progress
- [ ] **TODO 4.8**: Documentation updates
  - Update README with discovery mode examples and timings
  - Explain instant failover model (vs old ACDS election)
  - Document MEDIA_QUALITY_REPORT and HOST_QUALITY_UPDATE flow
  - Add troubleshooting guide for bandwidth/migration issues
  - Update man pages with new packet types
  - Add architecture diagrams for clarity

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
├── session.c
├── webrtc.h
└── webrtc.c
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
lib/network/acip/protocol.h   # Add new packet types (146-147: MEDIA_QUALITY_REPORT, HOST_QUALITY_UPDATE)
lib/network/acip/acds.h       # Add new message structures
src/discovery-server/         # Track host per session, broadcast HOST_DESIGNATED
src/server/main.c             # Use lib/session/host, add quality tracking
src/server/client.c           # Track per-frame metrics for quality reports
src/client/main.c             # Use lib/session/participant, add quality measurement
src/client/protocol.c         # Handle MEDIA_QUALITY_REPORT and HOST_QUALITY_UPDATE
src/mirror/main.c             # Use lib/session/capture+display
src/discovery-server/server.c # Updated for migration tracking
lib/session/participant.c     # Add per-frame RTT tracking, quality window accumulation
lib/session/host.c            # Add quality metrics collection and election logic
CMakeLists.txt                # Add new source files (webrtc.c, media_quality metrics)
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
   - Host disconnect triggers automatic migration using pre-stored backup address
   - Backup host address learned from HOST_QUALITY_UPDATE (sent every 30-60s)
   - All participants know backup address locally, no ACDS query needed
   - Backup participant starts server immediately on known address/port
   - Other participants connect to backup within ~500ms total
   - Call resumes automatically (no user action required)
   - Single participant left → session ends cleanly
   - No ACDS dependency for failover (ACDS only receives notification)
   - Bandwidth measurements continue in background, re-election may occur after 30-60s

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
