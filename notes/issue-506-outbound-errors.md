# Issue #506: outbound failure boundary audit

This focused audit follows an ACIP packet from its producer through the outbound
worker to its transport backend. It excludes the parsing, SDP, palette, and log
formatting changes in PR #535, capability negotiation (#269), and demos (#431).

| File / function | Finding / disposition |
| --- | --- |
| `lib/network/acip/backpressure.c`: `outbound_worker` | Backend status already uses `asciichat_error_t`, but handling its errno scope discards the source message, system errno, and source location before producers can inspect them. Snapshot the current send's matching error before handling the scope; synthesize context for a backend returning only a status or unrelated diagnostic. |
| Same: `outbound_record_failure_and_fail_pending` | Queued controls lose the backend status to `ERROR_NETWORK`. Retain the first terminal status and a self-contained cause snapshot for the connection, and complete queued waiters with that status. Reliable packet expiry gets `ERROR_NETWORK_TIMEOUT` context. |
| Same: `acip_outbound_submitv` | The active waiter previously got only generic context; later producers got a generic network status. Import the retained snapshot into each caller's errno registry, including queued and later producers. |
| Same: `acip_outbound_destroy` | Keep existing teardown and producer-quiescence contract. Cleanup must not overwrite the first backend failure. |
| `lib/network/acip/send.c`: `acip_transport_sendv` | Preserves typed failures; adapts nonaccepted admission to `ERROR_BUFFER_FULL`. No conversion needed. |
| `include/ascii-chat/network/acip/backpressure.h`: admission API | Congestion and keyframe recovery remain admission outcomes, not terminal backend failures. Document failure propagation. |
| `lib/network/tcp/transport.c`: `tcp_sendv`, `tcp_recv`; `lib/network/webrtc/transport.c`: `webrtc_send`, `webrtc_recv` | Some lower-level causes are wrapped as network failures. Deferred: partial writes, consumed crypto nonces, receive framing, and connection lifecycle need their own transport-specific behavioral qualification. This change preserves the status the backend actually returns; it never retries a possibly partial packet. |
| `lib/network/network.c`: `send_with_timeout`, `recv_with_timeout`; `lib/network/webrtc/webrtc.c`: `webrtc_get_rtc_id`; WebSocket/library callbacks | Byte counts, IDs, EOF, and callback return conventions are genuine ABI semantics. Do not mechanically convert these to error enums. |

Nested backend wrappers retain their relevant causal diagnostic (preferring system
error metadata) alongside the terminal frame. Producer imports include both, so
the real TCP send failure preserves its low-level system error and source as well
as the transport wrapper. The fixture injects explicit system metadata through
the errno API instead of depending on platform-specific error-state storage.

No public structure, callback signature, platform ABI, boolean query, or count
return changes. Cancellation/interrupt semantics remain a separate lifecycle
concern; this patch adds no new cancellation status.

The requested API follow-up renames all 35 error-state operations to errno_*
functions, including the setter family and associated get/has/clear operations.
Callers and library consumers must rebuild; no old function aliases remain.
Error/scope/history types, constants, and header filenames retain their names.
The outbound helper is now outbound_record_failure_and_fail_pending: it retains
the first terminal cause, rejects later admissions, drains queued owners, and
wakes pending producers. The real TCP regression verifies EPIPE, socket_sendv's
source/message, tcp_sendv's wrapper, and their producer-local causal link.
The coordinated teardown correction interrupts active I/O, joins the outbound
worker, then closes its backend. This prevents the destructor and a failed-send
worker from concurrently joining a WebSocket service thread. A focused shutdown
contract covers serialization of those backend close calls.

Regression coverage in `tests/contracts/network/backpressure_test.c` checks
unsupported and fatal backend errors, timeout status with unrelated diagnostics,
two concurrent control producers, later admission after failure, preserved system
errno/source context, and congestion recovery without closing the backend.
The TCP unit suite additionally verifies system-cause propagation from a real
closed socket through the production send backend and outbound worker.

Merging current master for CI exposed a missing WASM definition of the existing
`terminal_get_effective_color_mode` API, now called by logging. The browser
terminal implementation returns its explicit color option or AUTO, consistent
with its existing always-color-capable output. This small compatibility fix is
needed to rebuild the source-fingerprinted Release bundle; no logging/parser
implementation from #535 is changed by this PR.
Browser artifact checks also execute C client initialization and cleanup. The
Release bundle uses Emscripten's default dlmalloc allocator, validated by all six
asset/initialization cases and the visible mirror/raster checks.
