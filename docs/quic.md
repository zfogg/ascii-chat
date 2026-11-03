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
