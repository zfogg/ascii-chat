import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";

export default function Network() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Network", path: "/docs/network" },
    ]);
  }, []);
  return (
    <div className="bg-gray-950 text-gray-100 flex flex-col">
      <div className="flex-1 flex flex-col docs-container">
        <header className="mb-12 sm:mb-16">
          <h1 className="heading-1 mb-4">
            <span className="text-green-400">🌐</span> Network & ACIP Protocol
          </h1>
          <p className="text-lg sm:text-xl text-gray-300">
            ACIP protocol architecture, ACDS discovery, NAT traversal, and P2P
            connections
          </p>
        </header>

        {/* ACIP Protocol Overview */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-cyan-400">⚡ ACIP Protocol</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">What is ACIP?</h3>
            <p className="docs-paragraph">
              ACIP (ascii-chat Internet Protocol) is a custom binary protocol
              designed specifically for low-latency, encrypted terminal-based
              video conferencing. Unlike HTTP, RTP, RTSP, or VNC, ACIP is
              optimized for:
            </p>
            <ul className="space-y-1 text-gray-300 text-sm ml-4 list-disc">
              <li>Efficient binary encoding (22-byte headers)</li>
              <li>Encryption by default (XSalsa20-Poly1305 AEAD)</li>
              <li>
                Terminal awareness (color depth, dimensions, capabilities)
              </li>
              <li>Multi-client conferencing with server-side rendering</li>
              <li>P2P connections with automatic host election</li>
            </ul>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">Packet Structure</h3>
            <p className="docs-paragraph">
              Every ACIP packet follows a consistent 22-byte header format:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "Packet Header (22 bytes):\n├─ Magic        (8 bytes) = 0xA5C11C4A1 (ASCIICHAT in hex)\n├─ Type         (2 bytes) = packet type (1-6199)\n├─ Length       (4 bytes) = payload size\n├─ CRC32C       (4 bytes) = hardware-accelerated checksum\n└─ Client ID    (4 bytes) = source client (0 = server)\n\nMax packet size: 5 MB\nData channel max: 16 KB (sufficient for compressed frames)"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">Packet Types</h3>
            <div className="space-y-4">
              {/* Type 1: Protocol Negotiation */}
              <div className="card-standard accent-blue">
                <h4 className="text-blue-300 font-semibold mb-2">
                  Type 1: Protocol Negotiation
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">1</code> = PROTOCOL_VERSION
                  (UNENCRYPTED)
                  <br />
                  Version and capabilities negotiation. Exchanged before
                  encryption setup to ensure both peers support compatible
                  features.
                </p>
              </div>

              {/* Types 1000-1203: Cryptographic Operations */}
              <div className="card-standard accent-orange">
                <h4 className="text-orange-300 font-semibold mb-2">
                  Types 1000-1109: Crypto Client Hello & Handshake
                </h4>
                <p className="text-gray-400 text-sm mb-3">
                  <strong>🔒 ALWAYS UNENCRYPTED</strong> - Exchanged before
                  encryption keys are established. Uses X25519 (ECDH) for key
                  agreement and Ed25519 for authentication. For detailed
                  information about the crypto protocol, see the{" "}
                  <TrackedLink
                    to="/docs/crypto"
                    label="Cryptography"
                    className="text-cyan-400 hover:text-cyan-300 transition-colors"
                  >
                    Cryptography
                  </TrackedLink>{" "}
                  documentation.
                </p>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">1000</code> =
                  CRYPTO_CLIENT_HELLO (Client → Server)
                  <br />
                  Expected server key fingerprint for multi-key selection
                  <br />
                  <br />
                  <code className="code-inline">1100</code> =
                  CRYPTO_CAPABILITIES (Client → Server)
                  <br />
                  Supported algorithms, compression methods
                  <br />
                  <br />
                  <code className="code-inline">1101</code> = CRYPTO_PARAMETERS
                  (Server → Client)
                  <br />
                  Chosen algorithms and cryptographic parameters
                  <br />
                  <br />
                  <code className="code-inline">1102</code> =
                  CRYPTO_KEY_EXCHANGE_INIT (Server → Client)
                  <br />
                  Server's ephemeral X25519 public key [32 bytes]
                  <br />
                  <br />
                  <code className="code-inline">1103</code> =
                  CRYPTO_KEY_EXCHANGE_RESP (Client → Server)
                  <br />
                  Client's ephemeral X25519 public key [32 bytes]
                  <br />
                  <br />
                  <code className="code-inline">1104</code> =
                  CRYPTO_AUTH_CHALLENGE (Server → Client)
                  <br />
                  Random nonce [32 bytes] for authentication
                  <br />
                  <br />
                  <code className="code-inline">1105</code> =
                  CRYPTO_AUTH_RESPONSE (Client → Server)
                  <br />
                  HMAC over shared secret proving client identity
                  <br />
                  <br />
                  <code className="code-inline">1106</code> = CRYPTO_AUTH_FAILED
                  (Server → Client)
                  <br />
                  Authentication rejected (invalid key or signature)
                  <br />
                  <br />
                  <code className="code-inline">1107</code> =
                  CRYPTO_SERVER_AUTH_RESP (Server → Client)
                  <br />
                  Server proves knowledge of shared secret
                  <br />
                  <br />
                  <code className="code-inline">1108</code> =
                  CRYPTO_HANDSHAKE_COMPLETE (Server → Client)
                  <br />
                  Encryption ready, session begins
                  <br />
                  <br />
                  <code className="code-inline">1109</code> =
                  CRYPTO_NO_ENCRYPTION (Client → Server)
                  <br />
                  Request to proceed without encryption (plaintext mode)
                </p>
              </div>

              {/* Types 1200-1203: Crypto Rekeying */}
              <div className="card-standard accent-violet">
                <h4 className="text-violet-300 font-semibold mb-2">
                  Types 1200-1203: Crypto Rekeying & Encryption
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">1200</code> = PACKET_ENCRYPTED
                  <br />
                  Encrypted session packet wrapper (XSalsa20-Poly1305 AEAD).
                  After handshake completion, all packet types ≥ 2000 are
                  wrapped in PACKET_ENCRYPTED before transmission. The original
                  packet type is included in the encrypted payload, not the
                  packet header.
                  <br />
                  <br />
                  <code className="code-inline">1201</code> =
                  CRYPTO_REKEY_REQUEST (Initiator → Responder)
                  <br />
                  Request new ephemeral key [32 bytes]
                  <br />
                  <br />
                  <code className="code-inline">1202</code> =
                  CRYPTO_REKEY_RESPONSE (Responder → Initiator)
                  <br />
                  New ephemeral key [32 bytes]
                  <br />
                  <br />
                  <code className="code-inline">1203</code> =
                  CRYPTO_REKEY_COMPLETE (Initiator → Responder)
                  <br />
                  Empty packet encrypted with NEW key (signals rekey complete)
                </p>
              </div>

              {/* Types 2000-2004: Messages */}
              <div className="card-standard accent-red">
                <h4 className="text-red-300 font-semibold mb-2">
                  Types 2000-2004: Message Packets
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">2000</code> = SIZE_MESSAGE
                  <br />
                  Terminal size change notification
                  <br />
                  <br />
                  <code className="code-inline">2001</code> = AUDIO_MESSAGE
                  <br />
                  Audio-related informational message
                  <br />
                  <br />
                  <code className="code-inline">2002</code> = TEXT_MESSAGE
                  <br />
                  General text message payload
                  <br />
                  <br />
                  <code className="code-inline">2003</code> = ERROR_MESSAGE
                  <br />
                  Error notification with error code and description
                  <br />
                  <br />
                  <code className="code-inline">2004</code> = REMOTE_LOG
                  <br />
                  Bidirectional remote logging for debugging
                </p>
              </div>

              {/* Types 3000-3001: Media Frames */}
              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Types 3000-3001: Media Frames
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">3000</code> = ASCII_FRAME
                  (Server → Client)
                  <br />
                  Complete terminal frame with ANSI color codes and cursor
                  position
                  <br />
                  <br />
                  <code className="code-inline">3001</code> = IMAGE_FRAME
                  (Client → Server)
                  <br />
                  RGB image data with dimensions. Auto-compressed with zstd if
                  exceeds 16KB
                </p>
              </div>

              {/* Types 4000-4001: Audio */}
              <div className="card-standard accent-yellow">
                <h4 className="text-yellow-300 font-semibold mb-2">
                  Types 4000-4001: Audio Streaming
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">4000</code> = AUDIO_BATCH
                  <br />
                  Multiple raw PCM audio samples bundled together for efficiency
                  (reduces packet overhead ~32x)
                  <br />
                  <br />
                  <code className="code-inline">4001</code> = AUDIO_OPUS_BATCH
                  <br />
                  Multiple Opus-encoded audio frames
                </p>
              </div>

              {/* Types 5000-5008: Control/State */}
              <div className="card-standard accent-green">
                <h4 className="text-green-300 font-semibold mb-2">
                  Types 5000-5008: Control/State Packets
                </h4>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">5000</code> =
                  CLIENT_CAPABILITIES (Client → Server)
                  <br />
                  Terminal dimensions, color depth, and feature flags
                  <br />
                  <br />
                  <code className="code-inline">5001</code> = PING (Keepalive)
                  <br />
                  Low-latency connectivity check (typically ~5s interval)
                  <br />
                  <br />
                  <code className="code-inline">5002</code> = PONG (Keepalive
                  response)
                  <br />
                  Immediate response to PING
                  <br />
                  <br />
                  <code className="code-inline">5003</code> = CLIENT_JOIN
                  <br />
                  Announces client can send media (video/audio)
                  <br />
                  <br />
                  <code className="code-inline">5004</code> = CLIENT_LEAVE
                  <br />
                  Clean disconnect notification
                  <br />
                  <br />
                  <code className="code-inline">5005</code> = STREAM_START
                  <br />
                  Client begins sending audio/video
                  <br />
                  <br />
                  <code className="code-inline">5006</code> = STREAM_STOP
                  <br />
                  Client stops sending audio/video
                  <br />
                  <br />
                  <code className="code-inline">5007</code> = CLEAR_CONSOLE
                  (Server → Client)
                  <br />
                  Clears entire terminal display
                  <br />
                  <br />
                  <code className="code-inline">5008</code> = SERVER_STATE
                  (Server → Client)
                  <br />
                  Broadcasts session state to all clients
                </p>
              </div>

              {/* Types 6000-6199: Discovery Mode */}
              <div className="card-standard accent-teal">
                <h4 className="text-teal-300 font-semibold mb-2">
                  Types 6000-6199: ascii-chat Discovery Service (ACDS)
                </h4>
                <p className="text-gray-400 text-sm mb-3">
                  Used in P2P WebRTC mode to establish connections through
                  discovery service. Find server keys at{" "}
                  <TrackedLink
                    to="https://discover.ascii-chat.com"
                    label="discover.ascii-chat.com"
                    className="text-cyan-400 hover:text-cyan-300 transition-colors"
                  >
                    discover.ascii-chat.com
                  </TrackedLink>
                </p>

                <p className="text-gray-400 text-sm mb-2">
                  <strong>Session Management (6000-6008):</strong>
                </p>
                <p className="text-gray-400 text-sm mb-3">
                  <code className="code-inline">6000</code> =
                  ACIP_SESSION_CREATE (Client → ACDS)
                  <br />
                  Create new session
                  <br />
                  <br />
                  <code className="code-inline">6001</code> =
                  ACIP_SESSION_CREATED (ACDS → Client)
                  <br />
                  Session created response
                  <br />
                  <br />
                  <code className="code-inline">6002</code> =
                  ACIP_SESSION_LOOKUP (Client → ACDS)
                  <br />
                  Find session by string
                  <br />
                  <br />
                  <code className="code-inline">6003</code> = ACIP_SESSION_INFO
                  (ACDS → Client)
                  <br />
                  Session info response
                  <br />
                  <br />
                  <code className="code-inline">6004</code> = ACIP_SESSION_JOIN
                  (Client → ACDS)
                  <br />
                  Join existing session
                  <br />
                  <br />
                  <code className="code-inline">6005</code> =
                  ACIP_SESSION_JOINED (ACDS → Client)
                  <br />
                  Joined response
                  <br />
                  <br />
                  <code className="code-inline">6006</code> = ACIP_SESSION_LEAVE
                  (Client → ACDS)
                  <br />
                  Graceful disconnect
                  <br />
                  <br />
                  <code className="code-inline">6007</code> = ACIP_SESSION_END
                  (Host → ACDS)
                  <br />
                  End session (host only)
                  <br />
                  <br />
                  <code className="code-inline">6008</code> =
                  ACIP_SESSION_RECONNECT (Client → ACDS)
                  <br />
                  Reconnect to existing
                </p>

                <p className="text-gray-400 text-sm mb-2">
                  <strong>WebRTC Signaling (6009-6010):</strong>
                </p>
                <p className="text-gray-400 text-sm mb-3">
                  <code className="code-inline">6009</code> = ACIP_WEBRTC_SDP
                  (Bidirectional)
                  <br />
                  Session Description Protocol (offer/answer)
                  <br />
                  <code className="code-inline">6010</code> = ACIP_WEBRTC_ICE
                  (Bidirectional)
                  <br />
                  ICE candidates for NAT traversal
                </p>

                <p className="text-gray-400 text-sm mb-2">
                  <strong>String Management (6020-6023):</strong>
                </p>
                <p className="text-gray-400 text-sm mb-3">
                  <code className="code-inline">6020</code> =
                  ACIP_STRING_RESERVE (Client → ACDS)
                  <br />
                  Reserve session string
                  <br />
                  <br />
                  <code className="code-inline">6021</code> =
                  ACIP_STRING_RESERVED (ACDS → Client)
                  <br />
                  Reservation confirmed
                  <br />
                  <br />
                  <code className="code-inline">6022</code> = ACIP_STRING_RENEW
                  (Client → ACDS)
                  <br />
                  Renew expiring reservation
                  <br />
                  <br />
                  <code className="code-inline">6023</code> =
                  ACIP_STRING_RELEASE (Client → ACDS)
                  <br />
                  Release string reservation
                </p>

                <p className="text-gray-400 text-sm mb-2">
                  <strong>Ring Consensus (6050-6051):</strong>
                </p>
                <p className="text-gray-400 text-sm mb-3">
                  <code className="code-inline">6050</code> =
                  ACIP_PARTICIPANT_LIST (ACDS → Participants)
                  <br />
                  Ordered participant ring
                  <br />
                  <br />
                  <code className="code-inline">6051</code> = ACIP_RING_COLLECT
                  (Participant → Next Participant)
                  <br />
                  Collect votes from participants
                </p>

                <p className="text-gray-400 text-sm mb-2">
                  <strong>Host Negotiation & Migration (6060-6068):</strong>
                </p>
                <p className="text-gray-400 text-sm">
                  <code className="code-inline">6060</code> =
                  ACIP_NETWORK_QUALITY (Participant → ACDS)
                  <br />
                  Bandwidth/RTT/jitter metrics
                  <br />
                  <br />
                  <code className="code-inline">6061</code> =
                  ACIP_HOST_ANNOUNCEMENT (Participant → ACDS)
                  <br />
                  I won host election
                  <br />
                  <br />
                  <code className="code-inline">6062</code> =
                  ACIP_HOST_DESIGNATED (ACDS → All Participants)
                  <br />
                  Discovery designates host
                  <br />
                  <br />
                  <code className="code-inline">6063</code> = ACIP_SETTINGS_SYNC
                  (Initiator → Host → All Participants)
                  <br />
                  Broadcast settings to peers
                  <br />
                  <br />
                  <code className="code-inline">6064</code> = ACIP_SETTINGS_ACK
                  (Participant → Initiator)
                  <br />
                  Settings acknowledged
                  <br />
                  <br />
                  <code className="code-inline">6065</code> = ACIP_HOST_LOST
                  (Participant → ACDS)
                  <br />
                  Current host disconnected
                  <br />
                  <br />
                  <code className="code-inline">6066</code> =
                  ACIP_FUTURE_HOST_ELECTED (ACDS → All Participants)
                  <br />
                  Next host elected
                  <br />
                  <br />
                  <code className="code-inline">6067</code> =
                  ACIP_PARTICIPANT_JOINED (ACDS → Existing Participants)
                  <br />
                  New peer joined
                  <br />
                  <br />
                  <code className="code-inline">6068</code> =
                  ACIP_PARTICIPANT_LEFT (ACDS → Remaining Participants)
                  <br />
                  Peer disconnected
                  <br />
                  <br />
                  <code className="code-inline">6100</code> =
                  ACIP_DISCOVERY_PING (Client → ACDS)
                  <br />
                  Keepalive to discovery server
                  <br />
                  <br />
                  <code className="code-inline">6199</code> = ACIP_ERROR (ACDS →
                  Client)
                  <br />
                  Generic error response from discovery server
                </p>
              </div>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">Compression</h3>
            <p className="docs-paragraph">
              Frames use zstd compression (configurable levels 1-9) to reduce
              bandwidth. Large frames are automatically compressed. RLE
              (Run-Length Encoding) optimizes identical pixels.
            </p>
          </div>
        </section>

        {/* Connection Flow */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-yellow-400">🔗 Connection Flow</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-yellow-300 mb-3">
              TCP Handshake & Crypto Setup
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "UNENCRYPTED HANDSHAKE:\n1. TCP handshake\n2. PROTOCOL_VERSION negotiation (version check)\n3. CRYPTO_CLIENT_HELLO (client key fingerprint)\n4. CRYPTO_CAPABILITIES exchange (algorithms)\n5. CRYPTO_KEY_EXCHANGE_INIT (server's ephemeral key)\n6. CRYPTO_KEY_EXCHANGE_RESP (client's ephemeral key)\n7. CRYPTO_AUTH_CHALLENGE (server nonce)\n8. CRYPTO_AUTH_RESPONSE (client signature)\n9. CRYPTO_SERVER_AUTH_RESP (server signature)\n10. CRYPTO_HANDSHAKE_COMPLETE (keys established)\n\nENCRYPTED SESSION:\n11. CLIENT_CAPABILITIES (terminal dims, color support) ← ENCRYPTED\n12. Server responds with session state ← ENCRYPTED\n13. Media frames begin ← ENCRYPTED\n\nKey Property: TCP guarantees packet order,\nACIP relies on this for frame integrity"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-orange-300 mb-3">
              Perfect Forward Secrecy
            </h3>
            <p className="docs-paragraph">
              Each session uses ephemeral X25519 keys that are discarded after
              use. Even if long-term keys are compromised, past sessions cannot
              be decrypted. Periodic key rotation for long-lived connections.
            </p>
          </div>
        </section>

        {/* Discovery Mode (P2P) */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-purple-400">
            🚀 Discovery Mode (WebRTC P2P)
          </h2>

          <p className="docs-paragraph">
            Discovery Mode is the default mode for ascii-chat, enabling
            zero-config P2P video calls using memorable session strings (e.g.,{" "}
            <code className="code-inline">purple-mountain-lake</code>). No port
            forwarding, no technical knowledge required. When you run the server
            without specifying a connection mode, it automatically generates a
            session string and registers with the discovery service. Clients
            connect using that session string. The official ascii-chat
            discovery-service is hosted at{" "}
            <code className="code-inline">
              discovery-service.ascii-chat.com
            </code>
            , with keys available at{" "}
            <TrackedLink
              to="https://discover.ascii-chat.com"
              label="discover.ascii-chat.com"
              className="text-cyan-400 hover:text-cyan-300 transition-colors"
            >
              discover.ascii-chat.com
            </TrackedLink>
            . The ascii-chat client downloads these keys over HTTPS, trusts
            them, and uses this ACDS server by default.
          </p>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">
              Phase 1: Instant Host Election (100ms)
            </h3>
            <p className="docs-paragraph">
              Two participants exchange NAT information and instantly elect a
              host. No bandwidth test blocking media startup.
            </p>
            <div className="space-y-3">
              <div className="card-standard accent-teal">
                <h4 className="text-teal-300 font-semibold mb-2">
                  NAT Priority Tiers (Best to Worst)
                </h4>
                <p className="text-gray-400 text-sm">
                  1. Localhost/LAN (same subnet, ~1ms latency)
                  <br />
                  2. Public IP (direct connection, ~10-50ms)
                  <br />
                  3. UPnP/NAT-PMP (port mapping, ~20-100ms)
                  <br />
                  4. STUN hole-punch (ICE connectivity, ~50-200ms)
                  <br />
                  5. TURN relay (always works, ~100-300ms)
                </p>
              </div>
              <p className="text-gray-400 text-sm">
                <strong>Bandwidth Override:</strong> If one participant has 10x+
                bandwidth, it becomes host regardless of NAT tier.
              </p>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              Phase 2: Media Starts (500ms total)
            </h3>
            <p className="docs-paragraph">
              WebRTC DataChannel established. All packets (control + media) flow
              through single DataChannel. Host begins rendering, participants
              begin capturing. No connection reset on migration.
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">
              Phase 3: Background Quality Measurement (30-60s)
            </h3>
            <p className="docs-paragraph">
              Bandwidth measured from real frames, not synthetic tests:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "Metrics collected:\n├─ Upload Kbps (frame sizes + frame loss)\n├─ RTT (round-trip time via frame timestamps)\n├─ Jitter (RTT variance)\n└─ Packet loss %\n\nScoring: score = (upload_kbps/10) + (100-loss%) + (100-rtt_ms)\nNo delay to media start — measurement is async"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">
              Phase 4: Optional Host Migration
            </h3>
            <p className="docs-paragraph">
              If a participant scores 20%+ higher quality, host migration
              happens transparently over the same DataChannel. Media resumes
              within ~100ms.
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-red-300 mb-3">Instant Failover</h3>
            <p className="docs-paragraph">
              Host broadcasts backup address every 30-60 seconds. If host dies:
            </p>
            <ul className="space-y-1 text-gray-300 text-sm ml-4 list-disc">
              <li>Backup participant becomes host immediately</li>
              <li>Other participants reconnect to stored backup address</li>
              <li>No ACDS query needed</li>
              <li>Recovery: ~300-500ms</li>
              <li>Media resumes automatically without interruption</li>
            </ul>
          </div>
        </section>

        {/* Basic Networking */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-teal-400">⚡ Traditional Mode Setup</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">
              Local Network Connection
            </h3>
            <p className="docs-paragraph">
              Direct P2P connection on same LAN (fastest, ~1-10ms latency):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Server: Bind to all interfaces (IPv4 + IPv6) on default port\nascii-chat server\n\n# Server: Bind to specific IPv4 and IPv6 interfaces together\nascii-chat server 192.168.1.50 '[2001:db8::1]'\n\n# Client: Connect to server via IPv4\nascii-chat client 192.168.1.50:27224\n\n# Client: Connect to server via IPv6\nascii-chat client '[2001:db8::1]:27224'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              Custom Ports & Advanced Options
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Server: Listen on custom port (all interfaces)\nascii-chat server --port 8080\n\n# Server: Bind to multiple addresses with custom port\nascii-chat server --port 8080 192.168.1.50 '[2001:db8::1]'\n\n# Client: Connect via IPv4 with custom port\nascii-chat client 192.168.1.50:8080\n\n# Client: Connect via IPv6 with custom port\nascii-chat client '[2001:db8::1]:8080'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">
              mDNS (Zero-Config LAN Discovery)
            </h3>
            <p className="docs-paragraph">
              Auto-discover servers on local network without knowing IP
              addresses:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Client: Scan for servers on local network via mDNS\nascii-chat client --scan\n\n# Server: Server can be discovered on local network\nascii-chat server\n\n# Client: Discover and list all available servers\nascii-chat client --scan\n\n# Discovery mode also supports mDNS for peer-to-peer discovery\nascii-chat client session-string --scan"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-orange-300 mb-3">
              Port Forwarding & External Access
            </h3>
            <p className="docs-paragraph">
              Enable connections from outside your network:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Server: Enable automatic UPnP/NAT-PMP port mapping\nascii-chat server --port-forwarding\n\n# Server: Port forwarding with custom port\nascii-chat server --port 8080 --port-forwarding\n\n# Client: Connect via hostname (supports IPv4 and IPv6)\nascii-chat client example.com:27224\n\n# Client: Connect to IPv6 address directly\nascii-chat client '[2001:db8::1]:27224'\n\n# Server with discovery and port forwarding\nascii-chat server --discovery --port-forwarding"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* ACDS */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-purple-400">
            🔍 ACDS Discovery Service
          </h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">What is ACDS?</h3>
            <p className="docs-paragraph">
              ascii-chat Discovery Service (ACDS) is a rendezvous server that
              helps clients find each other using memorable session strings.
              ACDS attempts NAT traversal (UPnP, STUN, TURN) and provides
              connection metadata. Crucially:{" "}
              <strong>ACDS never sees media</strong>— only connection
              information.
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">Session Strings</h3>
            <p className="docs-paragraph">
              Format: <code className="code-inline">adjective-noun-noun</code>
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "Examples:\n├─ happy-sunset-ocean\n├─ bright-forest-river\n├─ quick-silver-fox\n└─ silent-morning-sky\n\n16.7 million+ possible combinations\nEasy to speak, remember, type\nExpire when server disconnects"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              Discovery Mode: Server Registration
            </h3>
            <p className="docs-paragraph">
              Start a server to create a session string and register with ACDS.
              The session string is auto-generated and printed when the server
              starts:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Default: Start server and register with official ACDS\nascii-chat\n\n# With authentication (password + SSH key)\nascii-chat --password 'secret123' --key ~/.ssh/id_ed25519\n\n# With port forwarding for direct TCP\nascii-chat --port-forwarding\n\n# With custom ACDS server\nascii-chat --discovery-service discovery.example.com:27225"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-orange-300 mb-3">
              Discovery Mode: Client Connection
            </h3>
            <p className="docs-paragraph">
              Connect to a running server using its session string (no IP
              needed):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Connect using session string\nascii-chat happy-sunset-ocean\n\n# With authentication\nascii-chat happy-sunset-ocean --password 'secret123' --server-key github:username\n\n# Force TURN relay (very restrictive networks)\nascii-chat happy-sunset-ocean --webrtc-skip-stun\n\n# Custom discovery server\nascii-chat happy-sunset-ocean --discovery-service discovery.example.com"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* NAT Traversal */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-orange-400">🌍 NAT Traversal</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-yellow-300 mb-3">UPnP & NAT-PMP</h3>
            <p className="docs-paragraph">
              Works on ~70% of home routers. Automatic port forwarding enables
              direct TCP connections (lowest latency).
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">WebRTC STUN/TURN</h3>
            <p className="docs-paragraph">
              <strong>STUN:</strong> Discovers public IP:port mapping
              <br />
              <strong>ICE:</strong> Negotiates P2P connections through NAT
              <br />
              <strong>TURN:</strong> Relay fallback (works in 99% of networks,
              slightly higher latency)
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">
              3-Stage Connection Fallback
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "1. Direct TCP         → 3s timeout  (if server has public IP)\n2. WebRTC + STUN      → 8s timeout  (NAT hole-punching)\n3. WebRTC + TURN      → 15s timeout (relay, always works)\n\nTotal time to first connection: up to 26 seconds worst case\nTypical home networks: 1-2 seconds (direct or UPnP)"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Troubleshooting */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-yellow-400">🔧 Troubleshooting</h2>

          <div className="space-y-3">
            <div className="card-standard accent-red">
              <h4 className="text-red-300 font-semibold mb-2">
                Connection Refused
              </h4>
              <p className="text-gray-300 text-sm">
                Ensure server is running and port is accessible. Check firewall
                settings. If behind corporate proxy, enable TURN.
              </p>
            </div>
            <div className="card-standard accent-yellow">
              <h4 className="text-yellow-300 font-semibold mb-2">
                Authentication Failed
              </h4>
              <p className="text-gray-300 text-sm">
                Verify password or server key matches. For SSH key auth, ensure
                public key is in server's authorized_keys or GitHub account is
                correct.
              </p>
            </div>
            <div className="card-standard accent-cyan">
              <h4 className="text-cyan-300 font-semibold mb-2">
                Slow Connection
              </h4>
              <p className="text-gray-300 text-sm">
                Reduce frame rate: <code className="code-inline">--fps 20</code>
                . Disable audio:
                <code className="code-inline">--no-audio</code>. Enable lower
                color mode: <code className="code-inline">--color-mode 16</code>
                .
              </p>
            </div>
            <div className="card-standard accent-purple">
              <h4 className="text-purple-300 font-semibold mb-2">
                TURN Relay Only
              </h4>
              <p className="text-gray-300 text-sm">
                If connection quality is poor, you're likely on TURN relay
                (highest latency). Typical on corporate networks. Try: enable
                UPnP on your router, or use the same physical network.
              </p>
            </div>
          </div>
        </section>

        <Footer />
      </div>
    </div>
  );
}
