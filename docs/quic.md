# ACIP over QUIC

This document records the stream framing being implemented for issue #247.
The public QUIC transport and WebTransport session protocol are not yet available.

## Reliable streams

Each participant opens one unidirectional stream for each outgoing control,
audio, and visual flow. Each stream begins with eight bytes:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `ACQS` |
| 4 | 1 | Framing version, currently `1` |
| 5 | 1 | Traffic class: control `0`, audio `1`, visual `2` |
| 6 | 2 | Reserved; both bytes must be zero |

The remainder is a sequence of existing ACIP header-and-payload packets. The
ACIP header supplies each packet's length and CRC; there is no extra length
prefix. Receivers incrementally assemble complete packets and apply the ACIP
size limit and CRC checks. A FIN is valid only after a complete preamble and at
a packet boundary. Invalid preambles, invalid packets, and truncated FINs are
protocol errors. A connection must reject duplicate streams for a class and
direction; the endpoint will enforce this when streams are attached.

Ordering is guaranteed within a stream only. Handshake, rekey, stream setup,
and teardown are control operations. Media must not cross a control transition
until the receiver has acknowledged the state on which that media depends.
The endpoint must implement these readiness barriers before it sends media.

The class identifies a delivery flow and is not an authenticated assertion
about the packet inside it. Plaintext packet types are checked against the flow
before dispatch. Where ACIP encryption is active, classification is performed
before encryption, and the receiver must validate the decrypted packet against
the flow. Authentication, replay handling, datagrams, and fallback are not
defined by this framing alone and remain implementation work.

## TLS policy

The QUIC TLS context requires TLS 1.3 and disables early data. A server must
provide a matching certificate and private key. A client verifies the chain
against an explicitly supplied CA file or the system trust store and verifies
the expected DNS name or IP address. ALPN must match exactly before a session
is considered ready. These checks establish the identity of the TLS endpoint;
they do not replace ACIP authentication or protect data beyond an
intermediary that terminates QUIC. The endpoint must apply the existing ACIP
security policy and must not report readiness until both TLS and ACIP
handshakes have succeeded.

ngtcp2's OpenSSL connection reference is borrowed by the SSL object. The
reference and its connection must remain valid while callbacks may use them;
the SSL app data must be cleared before freeing SSL if the connection has
already been destroyed. The endpoint must also honor the lifetime of the
ngtcp2 OpenSSL context associated with the connection.

The private connection engine processes one UDP payload at a time on a single
serialized lane. It uses monotonic nanosecond timestamps for QUIC timers and
rejects timestamps that move backwards. Once it produces a packet, its owner
must retain that packet if the socket cannot accept it and report successful
transmission before requesting another packet; this updates ngtcp2's pacing
state. TLS readiness from this engine does not imply ACIP authentication.

Reliable packets are copied into a per-connection queue and retained until
ngtcp2 reports their bytes acknowledged. The queue charges buffer capacity
and queue metadata against an 8 MiB budget. Visual traffic leaves 512 KiB
each for audio and control; audio leaves 512 KiB for control. The scheduler
selects pending control, then audio, then visual data, and can use another
stream when one is flow-control blocked. Congestion returns an explicit error
without admitting the packet. The owner must bound its receive queue and copy
each borrowed packet before the callback returns; a rejected callback fails
the connection instead of silently losing reliable data. Flow-control credit
is extended after that callback succeeds.

The current engine rejects stream resets and STOP_SENDING as connection
errors. A sender can queue FIN after the admitted packets on an open stream;
it must not submit further packets to that class. ngtcp2 retransmits a lost
FIN. A clean FIN closes that traffic stream at a packet boundary, and a
replacement stream with the same class is rejected. Graceful connection shutdown,
reconnection, and replay of application operations are not yet implemented;
a future reconnect must establish new TLS and ACIP sessions before admitting
media and must not automatically replay state-changing operations.

## Private UDP driver

The experimental driver binds a concrete IPv4 or IPv6 address and uses one
nonblocking UDP socket for up to 64 connections. It routes by destination
connection ID, including the original Initial ID and active IDs issued by
ngtcp2, rather than by peer address. The application drives it on one lane;
TLS contexts outlive the endpoint, the endpoint owns connections and its socket,
and callers must discard borrowed connection handles when removed or destroyed.
An accept callback can install a receiver before the Initial is processed.
The pump bounds work per call and retains a generated datagram when the socket
would block. A connection error identifies the affected handle so its owner can
remove it while retaining the other connections.

This driver is not a public listener: it lacks address-validation Retry,
production admission controls, wildcard-address destination selection,
version negotiation, a blocking wakeup mechanism, application authentication,
and a graceful close. Its 64-connection cap bounds state but is not sufficient
DoS protection. Do not expose it to untrusted networks yet. A socket loopback
fixture exercises two simultaneous connections and continued routing after one
is removed; it does not prove ACIP authentication or the public call flow.

The private transport adapter wraps one connection with the existing ACIP
send/receive interface. It copies vectored sends, exposes complete received
packets through a bounded queue, and reports QUIC as its transport type. It
returns an explicit unsupported error for datagrams and deadlines. The adapter
can use a borrowed lane mutex to serialize its operations with endpoint
pumping; the owner must hold that mutex for every direct connection and pump
operation. Receive remains nonblocking, and the adapter must be destroyed
before its borrowed connection and mutex. This does not yet provide the worker,
wakeup, or lifecycle integration required by concurrent client/server paths.

## ACIP encryption audit

The current ACIP `crypto_encrypt` uses a random 16-byte sender prefix followed
by a big-endian counter. Its counter, keys, rekey state, and accounting fields
are mutable and have no internal lock, so an integrating transport must
serialize access to a shared context, including rekey operations. The prefix
and counter avoid reuse when used correctly; they do not by themselves prevent
replay. `crypto_decrypt` authenticates the supplied nonce and ciphertext but
does not remember received nonces or validate a sender prefix. Replaying the
same valid encrypted ACIP packet can therefore succeed at this layer.

QUIC packet protection rejects QUIC-level replay between its endpoints, but a
QUIC terminator that forwards ACIP ciphertext is a different trust boundary.
The current ACIP encryption must be retained where end-to-end policy requires
it. Before enabling this driver as an ACIP transport, the protocol needs
authenticated per-flow replay state that tolerates independent reliable streams
and lossy datagrams, and coordinated key epochs across control transitions.
A single fixed window shared by all flows can reject an indefinitely delayed
reliable control packet after enough media arrives. Media must not pass a rekey
or setup barrier until the receiver has acknowledged the required state.

An experimental private security codec binds sender direction, traffic class,
delivery mode, and a per-flow sequence inside each ACIP-encrypted packet.
Reliable flows track their own monotonic receive sequence; media datagrams use
separate 64-packet replay windows. Authentication and packet validation happen
before receive state changes, and the codec rejects use during rekey or after
the borrowed crypto context changes key epoch. Its owner must serialize all
access to that context, retain exactly one security state for the epoch, and
establish an acknowledged barrier before replacing the state. The private
connection engine can enable it after TLS and a caller-confirmed ACIP handshake
barrier; it accepts only initial handshake and error control packets before that
point, rejects no-encryption and premature rekey messages, and requires this
security layer before permitting media. Once enabled it seals
and opens reliable packets before dispatch. A UDP loopback fixture exercises
the existing password-authenticated ACIP handshake through the private adapter,
waits for both peers and their queued handshake data, and then exchanges
encrypted media. The security transition is coordinated by the fixture rather
than negotiated by the application. The codec is not connected to a public
transport. It does not implement datagram fragmentation, rekey transitions,
or replay protection for existing connections.
