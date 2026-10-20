# Sync inspector activity

Counts measure operations, not time spent blocked or kernel system calls.
For conditions, L/s means waits and U/s means signals plus broadcasts; they
are not a balanced lock/unlock pair. `Change/s` counts actual atomic value
changes; other primitive types show `-`. Rows sort by L/s + U/s descending,
with mutexes in detected wait cycles always first. Ties sort by type and name.
`Total Actions` is cumulative: acquisitions plus releases for locks, waits plus
signals/broadcasts for conditions, and tracked loads/stores/CAS/RMW uses for
atomics (including unchanged stores and failed CAS). Inspector reads do not
add to atomic totals. Counts use decimal compact notation such as `57.4k`
and `1.3m`, rounded to one decimal place with trailing `.0` omitted.
`Total Wait` is elapsed wall-clock time with at least one thread inside a
condition wait, including current waits. Overlapping waiters count once, idle
intervals add nothing, and non-condition rows show `-`. It includes reacquiring
the associated mutex before the wait returns. Timing snapshots never acquire
that mutex. Names are capped at 30 columns; the selected full name appears in
the footer. State/value uses 12 columns, followed by a two-space break.

## Metadata and diagnostics

Logging and allocation attribution use per-thread copies of registry metadata.
An atomic revision invalidates each copy after registration/removal. Warm hits,
misses and type checks do not acquire the registry rwlock. Copies own their
strings; they never pin objects or authorize dereferencing cached addresses.
Actual object-state snapshots retain the registry's lifetime protection.

The condition checker scans at the five-second stuck-wait threshold, scheduling
earlier existing-wait deadlines and retrying busy snapshots. Already-stuck
conditions retain their 500 ms warning cadence. It takes two registry read
locks per completed scan. The mutex cycle detector still checks every 100 ms,
using fixed, serialized scratch storage instead of allocating lists per thread.

The visible inspector takes a protected registry snapshot four times per second.
With it hidden, the controlled mirror run averages 0.4 registry acquisitions/sec
from condition checks. Opening it adds approximately four acquisitions/sec.

## Rendering and input

Colorscheme readers cache a thread-owned scheme and compiled palette, invalidated
by the scheme revision, output mode and detected terminal background. Applying a
scheme still serializes writes and appears on the next read.

FFmpeg prefetch waits on frame consumption, seek completion and shutdown
notifications instead of polling every millisecond, including at EOF. Empty
consumer polls read atomic readiness/error flags without taking the mutex.
The producer publishes and checks its next wait predicate in one protected
section; its only release/reacquire around work keeps decoding outside the
handoff lock. Each delivered frame normally needs one consumer pair, one
producer pair, and a condition wait release/reacquire pair. Rewind explicitly
wakes EOF sleepers. Pause state is a registered atomic flag, with CAS toggles
to preserve concurrent pause-key changes; pause queries acquire no mutex.
The source-level seek mutex was only used by audio readers, so it did not
exclude seeks. Seek/reset now uses the decoder packet mutex, covering buffered
audio too. A seek generation discards pre-seek video decode completions while
preserving the current consumer image until its replacement arrives.

Inactive recording submissions return before locking. When recording is active,
`audio_recording` protects the mixer and its lifetime; the atomic fast-path hint
is always checked again under that lock. Other static locks are named
`audio_visualization_history` and `fftw_planner` for their actual purposes.

Mirror's latest-frame queue waits for notification with a bounded 100 ms timeout
instead of polling every millisecond. Three reusable frame slots keep capture,
queued and displayed images distinct; recording retains shared owning buffers.
Display-owned resize/flip/filter images and terminal assembly storage are reused
until dimensions/capacity change. Typical render rows use invocation-local stack
scratch, with a heap fallback for widths above 512. Mirror reuses its ASCII output
buffer. Three reference-counted UI snapshot slots copy each submission directly
into owned storage; an in-flight reader prevents its slot from being reused.
Capacity growth, effects and recording retain tracked allocation paths.

Local file playback refills its 150 ms audio queue below 100 ms instead of waking
on every device callback. Capture/AEC keeps its 10 ms cadence. File-only
transmission produces complete 20 ms audio frames on the media clock, without
acquiring the device wakeup guard. Audio callbacks wake the worker only for
capture/AEC or local monitoring.

## Connected media ownership and names

| Inspector name | Protected state or notification |
| --- | --- |
| shared_media_buffer_pool | Shared media buffer free lists, allocation budgets and accounting |
| reserved_control_buffer_pool | Buffer capacity reserved for handshake/error/control traffic |
| audio_processing_wait_guard | Device-driven audio worker sleep/shutdown coordination |
| audio_processing_wakeup | New capture/render work, local-playback refill or shutdown |
| connection_outgoing_packet_queue | Outgoing packet admission, ordering, priority, budget and completion state |
| outgoing_packet_ready_or_done | Queue became nonempty, synchronous control completed, or connection stopped |
| tcp_packet_io_lifetime_guard | Receive queue ownership and stream lifetime during transmission/close |

Connection-owned packet slots retain at most eight buffers of at most 256 KiB
in each receive, outgoing-plaintext and backend-write cache. Larger owners are
returned immediately; all retained capacity remains charged to the process
buffer budget and is released at connection teardown. Held receive references
prevent a slot from being reused. Sharing and releasing nonfinal references use
atomic reference counts; the final release still serializes pool accounting.
Exclusive receive owners decrypt in place, preserving the nonce until
authentication finishes; shared or borrowed envelopes retain the allocation
fallback. Framing, ciphertext, video-encoder and capture-image workspaces grow
only when needed. Capture reuses the same processed image for paused playback.

Native asynchronous transports already own a bounded outgoing queue, so their
audio capture worker submits there directly instead of waking a second sender
queue. Two 20 ms Opus frames normally share one network batch, with a 40 ms
partial-batch deadline. Device processing stays independent of video FPS.
Backpressure reads use the backend's atomically published queued-byte count;
they no longer acquire the TCP guard to poll the stream. Conditions notify on
empty-to-nonempty transitions and actual synchronous completions instead of
broadcasting after every asynchronous submission. Per-frame debug messages are
rate limited; log-history serialization remains for messages actually retained.
ASCII decode output is also retained by the client's receive worker; compressed
and plain frames share the same callback and checksum validation.

Inspector output is assembled in a fixed 64 KiB presentation-worker buffer,
then flushed as a frame with terminal synchronized-output markers. Exceptional
oversized frames flush in chunks. Rendering adds no allocation or mutex
dependency; terminal writes scale with frames/chunks rather than table rows.

Loop/seek cancellation stops the old packet reader before clearing the seek
interrupt flag. The seek itself remains interruptible by application shutdown,
and clears only a cached `AVERROR_EXIT` from the canceled read. Keeping the seek
flag set during `av_seek_frame()` could cancel the seek's own I/O and make a
later file loop fail as a background decoding error.

The outgoing queue still needs producer/consumer handoffs and condition wait
release/reacquisition. Incoming audio and video also cross the I/O/receive-worker
boundary. These operations scale with packets, including audio delivered
independently of video. A displayed acquisition is not a kernel call: Windows
first tries `TryEnterCriticalSection`, blocking only when that attempt fails.

Controller geometry and screen coverage are atomically published for input readers; queries
never borrow a screen snapshot or acquire its mutex. Empty input probes decode
terminal replies and retain the next key in order before taking the routing
mutex. Buffered keys, paste state and editor deadlines still use serialized
routing. Mirror's idle input cadence is 10 ms; static presentation geometry
checks are bounded at 100 ms and new frames wake presentation immediately.

## Verification

`tests/integration/sync_tui_modes.py` checks inventories, navigation, resizing,
colors and activity in mirror, connected client and discovery modes, with
generated changing video and audio. Its artifact directory contains actual PTY
cells as PNG/text, raw ANSI, logs and per-mode activity JSON. These controlled
15 FPS results vary with device callbacks, workload and logging verbosity.

Native regressions cover cache removal/reuse, empty lookups, zero-lock warm
palette reads, allocation-free deadlock scans, frame-slot ownership, resize
workspace output equivalence (including wide heap fallback), 1,000 warmed ASCII
conversions and UI submissions with an older snapshot held in flight, non-consuming input
probes, Escape timing, paste/editor routing and zero-lock idle UI queries.

A verified 15 FPS mirror sample, with the inspector open, measured approximately:

| Primitive | Acquisitions/sec (or condition waits/sec) |
| --- | ---: |
| named_registry_lock | 4 (0.4-0.5 across a hidden interval) |
| colorscheme | 0 |
| shared_media_buffer_pool | 0 |
| timer_manager | 0 |
| display | 47 |
| ffmpeg_prefetch | about 45 |
| queue_not_empty | 16 |
| debug_memory | 0 after warm-up |
| audio_processing_wait_guard | 31-39 |
| ui_controller | 31 |
| ui_input | 0 |

The display queue performs roughly three acquisitions per video frame. Warm
mirror rendering no longer allocates ASCII output or UI snapshots per frame.
Connected client/discovery workloads additionally own network and
audio buffers; the zero mirror buffer-domain result is not a claim about those
transport paths.
