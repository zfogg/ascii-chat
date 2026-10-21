# Issue #506: outbound failure boundary audit

This focused audit follows an ACIP packet from its producer through the outbound
worker to its transport backend. It excludes the parsing, SDP, palette, and log
formatting changes in PR #535, capability negotiation (#269), and demos (#431).

| File / function | Finding / disposition |
| --- | --- |
| `lib/network/acip/backpressure.c`: `outbound_worker` | Backend status already uses `asciichat_error_t`, but handling its errno scope discards the source message, system errno, and source location before producers can inspect them. Snapshot the current send's matching error before handling the scope; synthesize context for a backend returning only a status or unrelated diagnostic. |
| Same: `fail_queue` | Queued controls lose the backend status to `ERROR_NETWORK`. Retain the first terminal status and a self-contained cause snapshot for the connection, and complete queued waiters with that status. Reliable packet expiry gets `ERROR_NETWORK_TIMEOUT` context. |
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

Regression coverage in `tests/contracts/network/backpressure_test.c` checks
unsupported and fatal backend errors, timeout status with unrelated diagnostics,
two concurrent control producers, later admission after failure, preserved system
errno/source context, and congestion recovery without closing the backend.
The TCP unit suite additionally verifies system-cause propagation from a real
closed socket through the production send backend and outbound worker.
