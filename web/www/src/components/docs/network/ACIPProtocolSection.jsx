import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../../TrackedLink";

export default function ACIPProtocolSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        ⚡ ACIP Protocol
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          What is ACIP?
        </Heading>
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
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Packet Structure
        </Heading>
        <p className="docs-paragraph">
          Every ACIP packet follows a consistent 22-byte header format:
        </p>
        <CodeBlock language="bash">
          {
            "Packet Header (22 bytes):\n├─ Magic        (8 bytes) = 0xA5C11C4A1 (ASCIICHAT in hex)\n├─ Type         (2 bytes) = packet type (1-6199)\n├─ Length       (4 bytes) = payload size\n├─ CRC32C       (4 bytes) = hardware-accelerated checksum\n└─ Client ID    (4 bytes) = source client (0 = server)\n\nMax packet size: 5 MB\nData channel max: 16 KB (sufficient for compressed frames)"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Packet Types
        </Heading>
        <div className="space-y-4">
          {/* Type 1: Protocol Negotiation */}
          <div className="card-standard accent-blue">
            <Heading
              level={4}
              className="text-blue-300 font-semibold mb-2"
            >
              Type 1: Protocol Negotiation
            </Heading>
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
            <Heading
              level={4}
              className="text-orange-300 font-semibold mb-2"
            >
              Types 1000-1109: Crypto Client Hello & Handshake
            </Heading>
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
              <code className="code-inline">1101</code> =
              CRYPTO_PARAMETERS (Server → Client)
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
              <code className="code-inline">1106</code> =
              CRYPTO_AUTH_FAILED (Server → Client)
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
            <Heading
              level={4}
              className="text-violet-300 font-semibold mb-2"
            >
              Types 1200-1203: Crypto Rekeying & Encryption
            </Heading>
            <p className="text-gray-400 text-sm">
              <code className="code-inline">1200</code> = PACKET_ENCRYPTED
              <br />
              Encrypted session packet wrapper (XSalsa20-Poly1305 AEAD).
              After handshake completion, all packet types ≥ 2000 are
              wrapped in PACKET_ENCRYPTED before transmission. The
              original packet type is included in the encrypted payload,
              not the packet header.
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
            <Heading
              level={4}
              className="text-red-300 font-semibold mb-2"
            >
              Types 2000-2004: Message Packets
            </Heading>
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
            <Heading
              level={4}
              className="text-cyan-300 font-semibold mb-2"
            >
              Types 3000-3001: Media Frames
            </Heading>
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
            <Heading
              level={4}
              className="text-yellow-300 font-semibold mb-2"
            >
              Types 4000-4001: Audio Streaming
            </Heading>
            <p className="text-gray-400 text-sm">
              <code className="code-inline">4000</code> = AUDIO_BATCH
              <br />
              Multiple raw PCM audio samples bundled together for
              efficiency (reduces packet overhead ~32x)
              <br />
              <br />
              <code className="code-inline">4001</code> = AUDIO_OPUS_BATCH
              <br />
              Multiple Opus-encoded audio frames
            </p>
          </div>

          {/* Types 5000-5008: Control/State */}
          <div className="card-standard accent-green">
            <Heading
              level={4}
              className="text-green-300 font-semibold mb-2"
            >
              Types 5000-5008: Control/State Packets
            </Heading>
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
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-2"
            >
              Types 6000-6199: ascii-chat Discovery Service (ACDS)
            </Heading>
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
              <code className="code-inline">6003</code> =
              ACIP_SESSION_INFO (ACDS → Client)
              <br />
              Session info response
              <br />
              <br />
              <code className="code-inline">6004</code> =
              ACIP_SESSION_JOIN (Client → ACDS)
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
              <code className="code-inline">6006</code> =
              ACIP_SESSION_LEAVE (Client → ACDS)
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
              <code className="code-inline">6022</code> =
              ACIP_STRING_RENEW (Client → ACDS)
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
              <code className="code-inline">6051</code> =
              ACIP_RING_COLLECT (Participant → Next Participant)
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
              <br />I won host election
              <br />
              <br />
              <code className="code-inline">6062</code> =
              ACIP_HOST_DESIGNATED (ACDS → All Participants)
              <br />
              Discovery designates host
              <br />
              <br />
              <code className="code-inline">6063</code> =
              ACIP_SETTINGS_SYNC (Initiator → Host → All Participants)
              <br />
              Broadcast settings to peers
              <br />
              <br />
              <code className="code-inline">6064</code> =
              ACIP_SETTINGS_ACK (Participant → Initiator)
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
              <code className="code-inline">6199</code> = ACIP_ERROR (ACDS
              → Client)
              <br />
              Generic error response from discovery server
            </p>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Compression
        </Heading>
        <p className="docs-paragraph">
          Frames use zstd compression (configurable levels 1-9) to reduce
          bandwidth. Large frames are automatically compressed. RLE
          (Run-Length Encoding) optimizes identical pixels.
        </p>
      </div>
    </section>
  );
}
