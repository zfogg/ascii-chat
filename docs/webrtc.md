# WebRTC in ascii-chat

ascii-chat uses WebRTC for **peer-to-peer NAT traversal**, enabling connections between users behind firewalls and NAT routers without requiring port forwarding.

## Terminology

### Core WebRTC Concepts

| Term                | Meaning                                                       | ascii-chat Usage                                        |
|---------------------|---------------------------------------------------------------|---------------------------------------------------------|
| **WebRTC**          | Web Real-Time Communication - browser API for P2P media/data  | We use libdatachannel (C library) for DataChannels only |
| **Peer Connection** | Logical connection between two WebRTC endpoints               | One per remote participant in a session                 |
| **DataChannel**     | Bidirectional data pipe over WebRTC (like WebSockets but P2P) | Carries all ACIP packets (video frames, audio, control) |
| **Signaling**       | Out-of-band exchange of connection metadata                   | ACDS relays SDP/ICE between peers                       |

### ICE (Interactive Connectivity Establishment)

| Term                         | Meaning                                       | ascii-chat Usage                               |
|------------------------------|-----------------------------------------------|------------------------------------------------|
| **ICE**                      | Protocol for NAT traversal (RFC 5245)         | Automatic candidate gathering and pair testing |
| **ICE Candidate**            | A potential network path (IP:port + protocol) | Host, server-reflexive, and relay candidates   |
| **Host Candidate**           | Local IP address (direct, no NAT)             | First choice - fastest path                    |
| **Server-Reflexive (srflx)** | Public IP:port discovered via STUN            | NAT hole-punching path                         |
| **Relay Candidate**          | Traffic proxied through TURN server           | Fallback when direct/STUN fails                |
| **ICE Gathering**            | Process of discovering all candidates         | Runs in parallel with SDP exchange             |
| **Candidate Pair**           | Local + remote candidate being tested         | ICE agent tests all pairs, selects best        |

### STUN (Session Traversal Utilities for NAT)

| Term                  | Meaning                                    | ascii-chat Usage                 |
|-----------------------|--------------------------------------------|----------------------------------|
| **STUN**              | Protocol for NAT type discovery (RFC 5389) | Discovers public IP:port mapping |
| **Binding Request**   | STUN query: "what's my public address?"    | Sent to STUN server during ICE   |
| **Reflexive Address** | Your public IP:port as seen by STUN server | Used for srflx candidates        |

### TURN (Traversal Using Relays around NAT)

| Term                | Meaning                                                | ascii-chat Usage                          |
|---------------------|--------------------------------------------------------|-------------------------------------------|
| **TURN**            | Relay protocol when direct connection fails (RFC 5766) | Fallback for symmetric NAT                |
| **Allocation**      | Reserved relay address on TURN server                  | Server allocates IP:port for your session |
| **TURN Credential** | Username + password for TURN auth                      | HMAC-SHA1 time-limited credentials        |
| **Relay Address**   | TURN server's allocated address for you                | Other peer sends traffic here             |

### SDP (Session Description Protocol)

| Term                | Meaning                                                 | ascii-chat Usage                       |
|---------------------|---------------------------------------------------------|----------------------------------------|
| **SDP**             | Text format for media/connection negotiation (RFC 4566) | Negotiates terminal capabilities       |
| **Offer**           | First SDP message proposing capabilities                | Joiner sends to session creator        |
| **Answer**          | Response SDP accepting/modifying offer                  | Creator responds with selected mode    |
| **Media Line (m=)** | Describes one media stream                              | We use `m=application` for DataChannel |
| **Attribute (a=)**  | Additional parameters                                   | Terminal format, ICE credentials, etc. |

### SCTP (Stream Control Transmission Protocol)

| Term            | Meaning                             | ascii-chat Usage                    |
|-----------------|-------------------------------------|-------------------------------------|
| **SCTP**        | Transport protocol for DataChannels | Provides reliable, ordered delivery |
| **DTLS**        | Encryption layer under SCTP         | All DataChannel traffic encrypted   |
| **Reliability** | SCTP retransmits lost packets       | We use reliable, ordered channels   |

## Architecture

```
┌─────────────────┐                           ┌─────────────────┐
│  Session Host   │◄── WebRTC DataChannel ───►│  Session Client │
│   (Creator)     │                           │    (Joiner)     │
└────────┬────────┘                           └────────┬────────┘
         │                                             │
         │  SDP/ICE                           SDP/ICE  │
         │  signaling                         signaling│
         │                                             │
         └──────────────►┌───────┐◄────────────────────┘
                         │ ACDS  │
                         │Server │  (signaling relay only)
                         └───────┘
```

- **Star topology**: Session creator is the hub; clients connect to creator
- **ACDS**: Only relays signaling (SDP/ICE) - no media processing
- **Media flows P2P**: After ICE succeeds, all traffic goes directly between peers

## Connection Flow

```
Client (Joiner)              ACDS                 Server (Creator)
     │                         │                         │
     │──── JOIN_SESSION ──────►│                         │
     │◄─── TURN credentials ───│                         │
     │                         │                         │
     │     [ICE gathering starts]                        │
     │                         │                         │
     │──── SDP OFFER ─────────►│──── relay ─────────────►│
     │                         │◄─── SDP ANSWER ─────────│
     │◄─── relay ──────────────│                         │
     │                         │                         │
     │──── ICE candidates ────►│──── relay ─────────────►│
     │◄─── relay ──────────────│◄─── ICE candidates ─────│
     │                         │                         │
     │       [ICE connectivity checks in progress]       │
     │                         │                         │
     │◄════════ DataChannel Connected ══════════════════►│
     │                         │                         │
     │◄════════ ACIP packets (video/audio) ════════════►│
```

## 3-Stage Connection Fallback

ascii-chat automatically tries connection methods in order:

| Stage | Method        | Timeout | When It Works            |
|-------|---------------|---------|--------------------------|
| 1     | Direct TCP    | 3s      | Server has public IP     |
| 2     | WebRTC + STUN | 8s      | NAT allows hole-punching |
| 3     | WebRTC + TURN | 15s     | Always (relay fallback)  |

## Special: Terminal Capabilities as SDP "Codecs"

We repurpose SDP's codec negotiation for terminal rendering capabilities:

```
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
a=rtpmap:96 TRUECOLOR/1
a=rtpmap:97 256COLOR/1
a=rtpmap:98 16COLOR/1
a=rtpmap:99 MONO/1
a=fmtp:96 width=120;height=40;renderer=halfblock;charset=utf8
```

**Terminal Format Parameters** (via `a=fmtp`):
- `width`, `height` - Terminal dimensions
- `renderer` - foreground, background, halfblock
- `charset` - ascii, utf8
- `compression` - none, zstd
- `rle` - Run-length encoding
- `csi_rep` - CSI REP escape sequence support

**Audio** uses standard Opus codec parameters:
```
a=rtpmap:111 opus/48000/1
a=fmtp:111 minptime=20;usedtx=1;useinbandfec=1
```

## Key Files

| Path                                    | Purpose                                    |
|-----------------------------------------|--------------------------------------------|
| `lib/network/webrtc/webrtc.h`           | Peer connection API (wraps libdatachannel) |
| `lib/network/webrtc/peer_manager.h`     | Multi-peer connection management           |
| `lib/network/webrtc/ice.h`              | ICE candidate handling                     |
| `lib/network/webrtc/sdp.h`              | SDP offer/answer generation                |
| `lib/network/webrtc/transport.c`        | ACIP transport over DataChannel            |
| `lib/network/webrtc/turn_credentials.c` | HMAC-SHA1 credential generation            |
| `src/client/connection_attempt.c`       | 3-stage fallback state machine             |
| `src/discovery-server/signaling.c`      | ACDS SDP/ICE relay                         |

## TURN Credential Security

TURN credentials are time-limited using HMAC-SHA1:

```
username = "{unix_timestamp}:{session_id}"
credential = base64(HMAC-SHA1(shared_secret, username))
```

- Default expiration: 24 hours from timestamp
- Credentials generated by ACDS on session join
- TURN server validates by recomputing HMAC

## Packet Types

WebRTC signaling uses ACIP packet types 110-111:

| Type                          | Value | Direction     | Purpose                |
|-------------------------------|-------|---------------|------------------------|
| `PACKET_TYPE_ACIP_WEBRTC_SDP` | 110   | Bidirectional | SDP offer/answer relay |
| `PACKET_TYPE_ACIP_WEBRTC_ICE` | 111   | Bidirectional | ICE candidate relay    |

## Dependencies

- **libdatachannel** - C WebRTC library (peer connections, DataChannels, ICE)
- **libsodium** - Ed25519 identity signatures
- **OpenSSL** - HMAC-SHA1 for TURN credentials, DTLS, libdatachannel links against it

## Why Not Media Channels?

WebRTC typically uses RTP for audio/video. We use DataChannels instead because:

1. **ASCII frames aren't video** - No standard codec for terminal grids
2. **Custom compression** - zstd on pre-rendered ASCII outperforms video codecs
3. **Simpler** - No media negotiation, transcoding, or bitrate adaptation needed
