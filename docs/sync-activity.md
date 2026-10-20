# Sync inspector activity

Lock/unlock counts measure operations, not time spent blocked. The debug build
also includes the cost of observing and formatting those operations.

The log formatter validates a registered type under one nonblocking read lock.
It does not copy the registry and lock again for each object. This query follows
registration and removal immediately, without a stale type cache.

The background condition deadlock checker previously enumerated every registered
object and took another registry lock per object every 100 ms, even for non-condition
objects. It now uses two try-read locks per scan: size the batch, then copy condition
state and names while their objects are protected. Checks and logging run after
unlocking. A growing or busy registry defers that scan to the next cycle.

Rendering already measures allocation, resize, glyph conversion, padding and
frame timing with local timestamps. Those measurements avoid global named timer
records. The client audio sender and encoder likewise use local timestamps.
The shared timer API remains available for timers that actually need names or
cross-thread start/stop.

The audio worker still wakes immediately on device and network notifications.
Its recovery/software-source timeout is 10 ms, matching an AEC frame, instead of
polling every 1 ms. A condition wait releases and reacquires its mutex, so one
worker iteration can account for two locks and two unlocks in the inspector.

`tests/integration/sync_tui_modes.py` checks steady-state worker/timer rates in
mirror, client and discovery while exercising generated video/audio. Its optional
artifact directory includes the captured inventory and per-mode activity JSON.
These are controlled workload checks; device callbacks, logging verbosity and
media/network rates affect the numbers in other sessions.
