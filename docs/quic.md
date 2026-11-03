# ACIP over QUIC

This document records the stream framing being implemented for issue #247.
The QUIC endpoints and WebTransport session protocol are not yet available.

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
about the packet inside it. Where ACIP encryption is active, classification is
performed before encryption, and the receiver must validate the decrypted
packet against the flow. Authentication, replay handling, datagrams, and
fallback are not defined by this framing alone and remain implementation work.

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
