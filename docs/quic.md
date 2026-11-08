# ACIP over QUIC

This document records the stream framing being implemented for issue #247.
The native server has an opt-in listener and native clients can select it with
an explicit `quic://` address. An opt-in HTTP/3 service on the same UDP listener serves
browser WebTransport clients.

The browser includes an incremental ACIP stream and ACQS preamble decoder,
ACQD datagram fragmentation and bounded reassembly, and a WASM interface to
the native authenticated per-flow QUIC security codec. The security interface
requires a completed ACIP handshake and returns the authenticated sequence for
datagram recovery. A browser bridge maps an established WebTransport session
to the three ACIP streams and media datagrams, with separate send queues and a
bounded receive path. A channel can run the existing WASM ACIP handshake,
authenticate packets against their flow, enforce the READY/ACK barrier, and
request keyframes after visual loss. Its owner must initialize the shared WASM
client and configure credentials before starting the handshake. In the direct
browser client, the URL expresses the transport choice:

- `quic://chat.example:27224` selects QUIC explicitly, using the browser's
  HTTPS WebTransport endpoint. An omitted port defaults to 27224 and an omitted
  path defaults to `/acip`. Explicit paths and queries are preserved.
- `ws://localhost:27226` and `wss://chat.example:27226` select WebSocket explicitly.
  An HTTPS page generally requires WSS instead of WS.
- `https://chat.example:27224/acip` selects Auto and is the default endpoint
  form. Auto prefers WebTransport and can use a separately configured WSS
  fallback URL with a pinned server public key. It does not guess another port.

Explicit QUIC and WebSocket URLs do not switch transports. Auto fallback keeps ACIP
application encryption enabled and uses the same identity pin. It is attempted
only when the browser lacks WebTransport or its HTTP/3 session remains pending
for ten seconds. Explicit connection failures, including certificate errors,
and failures after HTTP/3 readiness do not trigger fallback. Browsers do not
expose enough detail to safely classify every UDP failure, so an immediate
opaque failure is reported rather than retried.

For example, start a listener with `--quic-bind-address 127.0.0.1`,
`--quic-port`, `--quic-tls-cert`, and `--quic-tls-key`, then connect with
`ascii-chat client quic://localhost:PORT --quic-ca-file CA.pem`. The client
uses system trust when `--quic-ca-file` is omitted and verifies the certificate
against the address hostname. An explicit QUIC connection does not fall back
by default. Clients can set `--quic-fallback-port PORT` and `--server-key KEY`
to retry the same hostname over TCP when the QUIC TLS connection times out.
Fallback requires ACIP encryption and authentication with the pinned server
key. Certificate, authentication, protocol, and cancellation errors do not
trigger fallback, and no fallback happens after TLS is established. Hostname
resolution currently uses IPv4; IPv6 literals are supported.

## Discovery route

A server can advertise separate native QUIC and browser WebTransport endpoints
when registering with ACDS. Enable its native QUIC listener as above, and set
`--webtransport-public-url https://HOST:PORT/acip` alongside the WebTransport
listener options below. This URL must be reachable by participants; the bind
address is not used as a public address. Endpoint metadata follows the existing
address disclosure policy and is withheld when the host changes or migrates.

Discovery mode can also create the host itself. Its creator accepts the same
`--quic-bind-address`, `--quic-port`, TLS certificate/key, and WebTransport listener
options as server mode. The initial session registration advertises the configured
TCP and native QUIC ports separately, plus the explicit WebTransport public URL.
`--discovery-expose-ip` still controls disclosure. ACDS derives the host address
from the authenticated creator connection instead of advertising loopback.
The creator starts its own listeners before attempting any media connection;
it does not connect to its newly advertised endpoint as a remote participant.
The shared listener helper owns startup validation and is closed with the host's
network server, including partially initialized listener cleanup.

A native participant opts into `--discovery-quic` and uses the advertised QUIC
port. With older ACDS servers that return no transport metadata, this explicit
option uses the advertised TCP port as the UDP port. Use `--quic-ca-file` for a
private trust anchor and `--server-key` to pin the application identity. The
media datagram options and authenticated pre-TLS TCP fallback also apply.

A browser discovery participant automatically prefers an advertised HTTPS
WebTransport endpoint when its browser supports WebTransport. ACDS signaling
and handoff remain on their original authenticated connection; the media
connection owns an independent ACIP handshake and per-flow encryption state.
An explicit TLS, authentication, or protocol failure is terminal. Hosts without
an advertisement, and browsers without WebTransport, retain WebRTC discovery.
The discovery Playwright tests cover both routes. The all-QUIC case prohibits
creating an RTCPeerConnection, checks browser upload and rendered server media
alongside two authenticated native QUIC participants, and checks native media
continues after the browser leaves. Both cases run past the host health
freshness window.

The optional version-1 transport extension follows the unchanged legacy
SESSION_CREATE payload (including its reserved session name) and SESSION_JOINED
payload. Integers in this extension use network byte order:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `ACQT` |
| 4 | 1 | Version `1` |
| 5 | 1 | Reserved, zero |
| 6 | 2 | Native QUIC UDP port; zero means unadvertised |
| 8 | 2 | HTTPS WebTransport URL byte length, at most 511 |
| 10 | variable | URL bytes, without a NUL terminator |

Legacy responses without the extension remain valid. Unknown versions,
truncated or trailing bytes, embedded NULs, and non-HTTPS URLs are rejected.
ACDS stores the extension in the same transaction as the session and keys.

## Discovery host changes

A prepared successor announces its configured QUIC port and WebTransport URL
using the same optional ACQT suffix on HOST_ANNOUNCEMENT. ACDS validates the
committed generation, round, participant, and session before atomically storing
the host endpoint, transport metadata, and metadata owner. Advertisements are
hidden during migration and when their owner differs from the current host.
Clearing a failed established host deletes its transport metadata. A conflicting
endpoint announcement for the same committed decision is rejected.

HOST_DESIGNATED carries the suffix only when host IP disclosure is allowed.
ACDS sends the extended designation followed by the unchanged fixed-size legacy
designation. Updated clients apply the first authoritative decision and ignore
the duplicate; old fixed-size parsers can consume the legacy message. New joins
and replayed designations use the current host's transport metadata.

Native QUIC participants retain their discovery session across a transient
transport closure and reconnect through the existing handoff state machine.
Authentication and protocol failures remain fatal. When a successor uses a
different identity, the participant's trusted key file must include that identity.
The protocol-version feature bit `0x0008` negotiates a bounded set of acceptable
Ed25519 identities in CLIENT_HELLO; older peers still receive the first key in
the original 32-byte format. The selected key must pass the ordinary signed
handshake and pin verification. Authentication failure cannot trigger TCP fallback.

`discovery-quic-handoff` exercises a prepared successor on distinct TCP and QUIC
ports with a different identity, reconnects an existing participant in the same
process, rejects a peer that trusts only the departed host, and verifies a late
join authenticates and receives video from the successor.

## HTTP/3 WebTransport listener

Enable the browser listener with `--webtransport-bind-address 127.0.0.1`,
`--quic-port PORT`, `--webtransport-origin https://example.com`,
`--quic-tls-cert CERT`, and `--quic-tls-key KEY`. The origin must exactly
match the browser page origin, including its port. The CONNECT path defaults
to `/acip` and can be set with `--webtransport-path`. Use a certificate trusted
by the browser and matching the listener hostname. Native raw QUIC and HTTP/3 share one UDP listener selected by `--quic-port`
(or the main server port when zero). ALPN selects `acip/1` or `h3` per
connection. TCP and UDP can use the same port number. If both bind-address
options are supplied, they must name the same address. There is no separate
WebTransport port flag.

The nghttp3 CONNECT endpoint checks origin and path, advertises HTTP datagrams
and WebTransport draft settings, and maps unidirectional WebTransport streams
and HTTP datagrams to ACIP flows. The application handshake and authenticated
per-flow encryption remain required on this listener. Closing the CONNECT
stream ends the session. The live browser integration probe can be run from
`web/web` with `pnpm test:webtransport:live -- <native-binary> <wasm-directory>`;
it needs OpenSSL, ssh-keygen, and a Playwright Chromium installation (or
`CHROME_PATH`). This probe covers pinned identity, origin and path rejection,
certificate rejection, encrypted control traffic, and fragmented browser-to-server
visual datagrams. It also runs two native QUIC participants alongside the browser
and checks media traffic continues after the browser disconnects. It also checks
WSS fallback when WebTransport is unavailable or UDP packets are dropped, and rejects
a wrong fallback identity key.

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
direction; the endpoint rejects duplicate streams when they are attached.

Ordering is guaranteed within a stream only. Handshake, rekey, stream setup,
and teardown are control operations. Media must not cross a control transition
until the receiver has acknowledged the state on which that media depends.
The endpoint enforces an authenticated READY/ACK barrier before it sends media.

The class identifies a delivery flow and is not an authenticated assertion
about the packet inside it. Plaintext packet types are checked against the flow
before dispatch. Where ACIP encryption is active, classification is performed
before encryption, and the receiver must validate the decrypted packet against
the flow. Authentication, replay handling, datagrams, and fallback are implemented above
this stream framing.

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
replacement stream with the same class is rejected. On local transport shutdown,
the endpoint sends a best-effort CONNECTION_CLOSE before detaching the receiver.
It does not retain the connection for close retransmission or wait for a draining
period, so loss or a blocked UDP socket can leave the peer until its idle timeout.
Full graceful shutdown, reconnection, and replay of application operations are
not yet implemented;
a future reconnect must establish new TLS and ACIP sessions before admitting
media and must not automatically replay state-changing operations.

## Private UDP driver

The experimental driver binds a concrete IPv4 or IPv6 address and uses one
nonblocking UDP socket for up to 64 connections. It routes by destination
connection ID, including the original Initial ID and active IDs issued by
ngtcp2, rather than by peer address. The application drives it on one lane;
TLS contexts outlive the endpoint, the endpoint owns connections and its socket,
and callers must discard borrowed connection handles when removed or destroyed.
An accept callback can attach an adapter while it holds the endpoint lane,
before the Initial is processed. Adapter methods must wait until that callback
returns and releases the lane.
The pump bounds work per call and retains a generated datagram when the socket
would block. A connection error identifies the affected handle so its owner can
remove it while retaining the other connections.

The UDP driver routes packets by connection ID, so a server can accept a
client's NAT rebinding and ngtcp2 validates the new peer path.
A process test changes the server-facing port of a loopback UDP proxy during a
call and checks path validation and continued media. Native applications can
also call `quic_endpoint_migrate()` to move a single-connection client
endpoint to another concrete local address in the same address family. Port
zero binds a new ephemeral UDP port. Producers must be paused during this
lifecycle operation; the endpoint stops and joins its worker before replacing
the socket, then restarts it. The TLS session, ACIP identity, keys, stream
positions, and replay state survive. ngtcp2 initiates immediate migration and
validates the new path. A failed bind or rejected migration keeps the old
socket and address. Worker restart failure instead closes the transports.
Migration requires a confirmed handshake, an unused peer connection ID, and
peer permission. Multi-connection client endpoints and cross-family migration
are not supported by this API. Automatic interface-change detection is left
to the application.

The native endpoint fixture moves an authenticated client to a new UDP socket,
using 127.0.0.2 when the OS permits that loopback alias and 127.0.0.1 otherwise. It verifies the server observes its new address, and exchanges media
datagrams and reliable control after the move. It also checks invalid addresses,
occupied local binds, listener rejection, and unchanged state after rejection.

After pumping, an owner can calculate the earliest QUIC timer and pending socket
write interest, then wait for UDP readiness outside the lane. A wake operation
sends a one-byte non-QUIC datagram to the bound local socket so another thread
can interrupt that wait after publishing work. The owner must keep the endpoint
alive until all wait and wake calls return, and pump after wakeup to drain it.

The listener sends QUIC Retry before allocating connection state. Its token is
authenticated with an endpoint-local random secret, bound to the client
address and Retry connection ID, and expires after ten seconds. Invalid tokens
are dropped. An admission callback returning `ERROR_BUFFER_FULL` drops that
attempt without stopping other connections.

This driver is not a public listener: it lacks production admission controls,
wildcard-address destination selection,
version negotiation, a blocking wakeup mechanism, application authentication,
and a graceful close. Its 64-connection cap bounds state but is not sufficient
DoS protection. Do not expose it to untrusted networks yet. A socket loopback
fixture exercises two simultaneous connections and continued routing after one
is removed; it does not prove ACIP authentication or the public call flow.

The private transport adapter wraps one connection with the existing ACIP
send/receive interface. It copies vectored sends, exposes complete received
packets through a bounded queue, and reports QUIC as its transport type. Its
private datagram path is opt-in; reliable deadlines remain unsupported. The adapter
can use a borrowed lane mutex to serialize its operations with endpoint
pumping; the owner must hold that mutex for every direct connection and pump
operation. The serialized adapter supports a timed receive and interrupt wakes
waiting receivers; the single-lane adapter remains nonblocking. Stop and join
all transport users before destroying it, and destroy it before its borrowed
connection and mutex. An endpoint owner can propagate a connection failure to
the adapter, waking receivers and preserving the error; it can then remove the
detached connection while transport users retain the failed adapter. A private
worker can pump and wait on an endpoint, wake for newly admitted sends, and
invoke a failure callback outside the lane. The callback must detach and remove
the failed connection before reporting it handled. Owners must stop producers,
clear their wakers, join the worker, and then destroy its borrowed resources.
The worker and adapter are not yet integrated into the production lifecycle or
connection selection.

The server can opt into a native listener with `--quic-bind-address`,
`--quic-tls-cert`, and `--quic-tls-key`. The bind address must be a concrete
numeric IPv4 or IPv6 address. `--quic-port` selects the UDP port; zero uses
the TCP server port. The TCP listener remains available. These flags configure
raw ACIP-over-QUIC, not browser WebTransport.

The native endpoint API wraps the UDP driver and worker. Returned transports
are owned by their caller and can be destroyed directly or released through the
endpoint, which must outlive them. It accepts resolved IPv4 or IPv6
addresses and requires a concrete local bind address. Accept is nonblocking and
may return a transport before TLS completes; the caller waits for negotiated
capabilities and runs the ACIP handshake. The shared client and server
negotiation helpers activate the security context and wait for authenticated
readiness before enabling media; direct low-level handshake users must perform
those steps explicitly. QUIC negotiation rejects modes without ACIP encryption.
Stop joins the worker,
interrupts claimed transports, and discards unclaimed accepts; callers destroy
claimed transports before destroying the endpoint. Endpoint lifecycle calls
must be serialized, and callers stop transport users before release. Public
hostname resolution, wildcard destination selection, graceful close, production
admission policy, and native client selection remain open.

## ACIP packet security

QUIC uses the shared [authenticated records and rotation](crypto-records.md)
implementation used by the other ascii-chat transports.

The private connection engine can enable it after TLS and a caller-confirmed ACIP handshake.
It accepts only initial handshake and error control packets before that point,
and rejects no-encryption and premature rekey messages. Each side then sends an
authenticated, versioned READY marker on the ordered control stream and replies
to the peer's marker with an authenticated ACK. A side may send application
traffic only after receiving the peer's ACK. This lets it know the peer has
processed its READY before media can arrive on an independent stream. A single
early encrypted control packet can be buffered while the local application
finishes its handshake; it must authenticate as READY once security is enabled.
After processing the peer's READY and sending ACK, the receiver can queue
authenticated media even if its own ACK is still in flight; this avoids
rejecting media reordered ahead of that ACK. The application still explicitly
enables outbound media after its own readiness exchange completes.
Later application state changes still require their own control barriers.

A UDP loopback fixture exercises the existing password-authenticated ACIP
handshake through the private adapter and then exchanges encrypted media after
the readiness exchange. Production native and browser transports invoke this transition after their
ACIP handshakes. Reliable sequences are checked independently per flow, and
datagrams use a replay window. The standalone worker fixture exercises a live
rotation, while the mixed native/browser test checks audio, rendered video, and
ping replies in epochs zero, one, and two. Enable its longer rekey run with
`ASCII_CHAT_QUIC_REKEY_TEST=1`; CI runs this variant. The discovery browser
fixture also uses that setting to wait for two rotations in all three host
connections, require continued browser rendering and native media, and reject
false ACDS host migration while the independent browser media context rekeys.

## Experimental datagram fragments

The [transport measurements](quic-benchmarks.md) compare TCP, WebRTC, reliable
QUIC, and QUIC datagrams under clean and impaired paths, including queue limits,
audio interruptions, visual age, and recovery. Large fragmented datagrams can
lose every visual packet during sustained impairment; reliable visual streams
can retain a substantial backlog while audio and control continue independently.

Native clients and servers can opt into outgoing audio datagrams with
`--quic-audio-datagrams` and visual datagrams with `--quic-visual-datagrams`.
Each endpoint makes these choices independently. If the peer did not negotiate
DATAGRAM support, media uses its reliable stream. Encoded visual packets with
a nonzero stream ID also use reliable delivery. Once a datagram is selected,
congestion or expiry does not retry it on the reliable stream. The sender gives
each packet a 100 ms deadline on the monotonic clock; this bounds local queues,
not its age at the receiver.

Raw `AUDIO_BATCH` payloads use a network-order batch header followed by
network-order signed 32-bit samples scaled from floating point. The native QUIC
fixture sends samples through the production encoder and both ACIP audio
handlers, exercising negotiated datagrams and reliable fallback.

The private connection can opt into QUIC DATAGRAM transport parameters before
the connection is created. It requires peer support and authenticated ACIP
readiness before media admission. The private fragment codec defines the
`ACQD` version 1 envelope for audio and visual packets. Each fragment starts with the following network-order
fields, followed by that fragment's bytes from one encrypted ACIP packet:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `ACQD` |
| 4 | 1 | Version `1` |
| 5 | 1 | Traffic class: audio `1`, visual `2` |
| 6 | 2 | Reserved, zero |
| 8 | 8 | Nonzero packet ID, unique per flow and key epoch |
| 16 | 4 | Total encrypted packet length |
| 20 | 2 | Payload size of each non-final fragment |
| 22 | 2 | Zero-based fragment index |
| 24 | 2 | Fragment count |
| 26 | 2 | Reserved, zero |

All fragments of a packet have identical metadata except index and payload.
The count must match the total and unit size; the last fragment is exactly the
remaining length. The reader accepts reordering and identical duplicates,
rejects malformed or conflicting fragments, and limits each flow to four
incomplete packets and 2 MiB of allocated buffer capacity. Incomplete packets
expire one second after their first received fragment. A caller must invoke
expiry even when no datagrams arrive. The current ACIP crypto codec limits a
plaintext to 1 MiB; larger media will require an explicit design rather than
assuming fragmentation removes that limit.

The browser selects reliable visual delivery for packets larger than 64 KiB,
before admission. This keeps large raw frames from requiring hundreds of lossy
fragments within the media deadline. Smaller media packets can use datagrams;
expired datagrams are never retried on a reliable stream.

The sender selects a fragment size from the negotiated peer limit and current
path payload size, leaving space for QUIC overhead. It discards the remainder
of a packet if the path shrinks below that size, and may discard queued media
whose explicit monotonic deadline expires. A UDP payload already handed to the
endpoint can still be sent after that deadline during socket backpressure.
The adapter exposes a negotiated capability snapshot, including a current-path
datagram size; callers must query it again after a path change. The outbound
scheduler preserves requested delivery and deadlines, discards expired media,
and marks an encoded stream as needing a keyframe if it drops a queued encoded
packet. The receiver drops older visual packets and detects gaps in the
authenticated visual sequence. After a gap, it suppresses encoded predictive
frames and sends a reliable authenticated keyframe request (packet type 1111,
containing a network-order 16-bit codec packet type and 32-bit stream ID).
The sender discards queued frames for that stream and requests an encoder
keyframe. Reordering can conservatively trigger the same recovery.

The envelope is protected by QUIC between transport endpoints, but it is not
authenticated across a terminating intermediary. Reassembly output must pass
the ACIP datagram security codec before dispatch or replay state changes.
Expiry bounds retained resources; it does not establish the age of a packet
whose first fragment arrives late. A missing final datagram is detected only
when a later visual packet arrives.
