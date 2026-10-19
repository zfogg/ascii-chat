# Runtime settings

Rendering changes apply while a call is running. CLI keyboard controls and the
browser settings panel use the same C validation and transaction machinery.
Changes affect the viewer who requested them. A viewer cannot change another
viewer’s palette, dimensions, colors, or frame rate.

The browser reports when the host accepts a change. Hosts may cap the requested
frame rate; the acknowledgment contains the actual value. Saved preferences stay
separate from effective remote settings, so a temporary server limit does not
rewrite the user’s preferences. Invalid requests leave all prior values intact.
Credentials, encryption, authentication, and session administration are outside
the rendering endpoint.

## Local C transactions

Use `options_apply_patch()` from `ascii-chat/options/runtime.h` for related edits:

```c
options_edit_t edits[] = {
  {.name = "color_mode", .type = OPTIONS_VALUE_INT,
   .value.integer = TERM_COLOR_TRUECOLOR},
  {.name = "fps", .type = OPTIONS_VALUE_INT, .value.integer = 30},
};
options_patch_t patch = {
  .edits = edits, .count = 2,
  .expected_revision = OPTIONS_REVISION_ANY,
  .origin = OPTIONS_ORIGIN_LOCAL,
};
asciichat_error_t error = options_apply_patch(&patch, NULL);
```

The writer mutex covers reading the latest state, copying it, applying every
edit, validating the final candidate, and publishing one immutable snapshot.
An invalid field rejects the whole transaction. No-op edits do not advance the
revision. Existing typed setters are single-edit transactions.

Use `options_toggle()`, `options_adjust()`, or `options_cycle()` for relative
keyboard actions. They compute the new value inside the transaction. When an
edit depends on a previously inspected state, capture it with
`options_read_snapshot()` and pass its revision as `expected_revision`; a stale
edit returns `ERROR_INVALID_STATE` and must be refreshed.

`options_revision()` lets an owner loop coalesce notifications. Read values and
revision together with `options_read_snapshot()`. Network sends, UI callbacks,
and observer notifications happen outside the options writer mutex.

Borrowed `options_get()` pointers remain valid until options shutdown. Retired
snapshots are retained until then: the previous fixed-capacity deferred list
could free a snapshot still held by a reader. Retention grows with successful
local changes (approximately `sizeof(options_t)` per change); remote rendering
updates use per-participant state and do not retain global snapshots.

## Browser state

`runtimeOptions(module)` returns a store for that specific C/WASM instance.
`applyPatch()` submits one JSON transaction. `getSnapshot()` returns committed
values and revision together. `subscribe()` observes changes from C keyboard
actions as well as JavaScript edits; React uses `useSyncExternalStore`.

There is no JavaScript palette/flip cache. Renderer and client modules do not
replace each other’s options accessors. UI submissions include only fields
changed since the preceding submission, preserving intervening engine changes.

## Per-participant protocol

All live remote rendering changes use `ACIP_SETTINGS_SYNC` (6063) and
`ACIP_SETTINGS_ACK` (6064), over the connection’s normal encrypted transport.
`CLIENT_CAPABILITIES` initializes terminal capabilities and advertises codec
support. Later codec advertisements cannot overwrite rendering preferences.
There is no feature negotiation or alternate runtime settings path. Hosts and
clients must both be updated for live rendering controls; older clients that
only send capability updates cannot change rendering after initialization.

Each payload begins with five network-order 32-bit integers:

| Offset | Field | Meaning |
| --- | --- | --- |
| 0 | schema | 1 |
| 4 | request | Connection-local correlation ID; 0 for initial host state |
| 8 | expected_revision | Host revision to compare, or `UINT32_MAX` |
| 12 | revision | Committed host revision in replies |
| 16 | status | `ASCIICHAT_OK` or an `asciichat_error_t` |

The remaining bytes contain a JSON object, limited to 4096 bytes. A header-only
request queries effective settings. Allowed fields are `width`, `height`, `fps`,
`color_mode`, `color_filter`, `render_mode`, `palette_type`, `palette_custom`, and
`matrix_rain`. Unknown fields, duplicate aliases, invalid types, oversized
payloads, invalid palettes, and stale revisions are rejected.

The host prepares dimensions, capabilities, and palette together before
publishing per-participant render state under its owner mutex. Every validly
framed request receives a full effective snapshot, including on rejection.
Clients correlate replies within the current connection and commit effective
state in C before exposing it to JavaScript.

`session_participant_request_settings()` submits this request;
`session_participant_receive_settings()` consumes the acknowledgment and invokes
the participant callbacks after releasing its settings mutex. It never applies
another viewer’s preferences to global options. Requests may retain the default
`TERM_COLOR_AUTO`: the participant resolves it to detected terminal capabilities
(or monochrome when detection is unreliable) before validating and sending,
without rewriting the caller’s preferences.

## Verification

`test-runtime-settings` is a standalone native test, including Windows. It checks
concurrent updates, atomic rejection, revision conflicts, snapshot lifetime,
animation exclusion, and independent remote viewers.

On Windows, `python tests/integration/runtime_settings_cli.py build/bin/ascii-chat.exe`
drives real keyboard controls in isolated hidden child consoles over TCP and
WebSocket. Browser coverage uses the rebuilt WASM artifacts and an isolated Vite
server: `vp exec playwright test -c tests/e2e/runtime-settings.config.ts` from
`web/web`. It checks native-host/browser rendering, independent viewers, C host
requests, invalid transactions, and the shared C keyboard UI.
