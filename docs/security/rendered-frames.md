# Received rendered frames

ACIP ASCII frames are network input. Native client and discovery sessions validate the entire decoded frame after length/CRC verification and before presentation callbacks, recording, snapshot completion or UI submission. Browser clients invoke the same C validator through WASM before decoding the accepted bytes as text. Browser zstd decoding has an explicit output capacity and requires the exact advertised output length.

## Content contract

A frame is a complete rectangle of advertised width by height terminal display cells. The dimensions are already present in the original 24-byte ASCII frame header; half-block sampling pixels do not double the terminal row count. Rows use LF or CRLF, with an optional final row delimiter. Spaces, completely black frames and repeated glyphs are valid. No entropy, aesthetics or default-palette heuristic is used.

UTF-8 is decoded with bounded utf8proc iteration and widths match the shared C grid composer. Invalid/overlong encodings, surrogate values, C0/C1 controls, DEL, embedded NUL and incomplete sequences are rejected. Combining codepoints require a preceding cell and at most 32 zero-width codepoints per cell. This follows the renderer's additive utf8proc width model rather than terminal-specific grapheme or emoji shaping.

Only bounded numeric CSI sequences used by rendering are permitted: supported SGR attributes/color forms, REP that fits the current row, and an optional initial cursor home. Each CSI has at most 16 parameters, 64 bytes and values no greater than 16384; RGB/indexed-color components are restricted to 0..255. OSC (including clipboard/title changes), DCS/APC, queries, private modes, arbitrary cursor movement and incomplete escapes fail the entire frame. Validation never strips or forwards a partial dangerous sequence. REP after combining marks is rejected because terminals disagree on its semantics. Colored/styled frames must return attributes to a full reset before ending, matching the renderer, so conceal/color settings cannot leak into subsequent local output.

Decoded text is limited to 16 MiB before allocation/decompression, and dimensions to 16384 cells per axis with a 16-Mi-cell total budget. The parser walks bytes once, uses constant-time ASCII membership and bounded Unicode lookup, and allocates no per-frame parser buffers. The existing receive/decompression workspace remains reusable.

## Palette metadata and compatibility

Flag 0x10 explicitly adds a versioned render-contract prefix after the original header and before the encoded text. All prefix integers are network-order uint32 values:

| Offset | Meaning |
| --- | --- |
| 0 | Schema, currently 1 |
| 4 | Effective render settings revision used to generate this frame |
| 8 | Render mode: foreground 0, background 1, half-block 2 |
| 12 | Effects: matrix rain 1, grid separators 2 |
| 16 | Palette byte count, 1..255 |
| 20 | Exact UTF-8 palette bytes, without NUL |

The original header's text sizes, compression flag and CRC continue to describe only text. Unknown schemas/effects/flags, malformed metadata, and trailing/missing text bytes are errors; an invalid new-format contract never falls back to legacy interpretation. The native server and shared discovery/browser host capture this metadata from the same immutable settings snapshot as the rendered frame. Header dimensions also come from that snapshot rather than a later settings read.

New-format glyphs must belong to that frame's palette, with spaces always allowed. Foreground and background modes both use palette glyphs. Half-block mode allows its actual block glyphs; grid separators are permitted only when declared. Matrix mutations are limited to the existing shared C mutation groups represented in the palette. Palette combining codepoints remain subject to UTF-8 and geometry checks. If the exact revision is acknowledged and UTF-8 settings are reliable, a contradictory palette is rejected. An older in-flight frame uses its own contract without being incorrectly checked against a newer acknowledgement. No unbounded revision history is needed.

Legacy flagless-contract frames retain strict control/UTF-8/geometry validation for interoperability, but cannot prove palette membership. The generic APIs that accept arbitrary pre-rendered strings remain legacy senders because they have no rendering snapshot. Updated rendering peers should be deployed together: older receivers do not understand the explicit prefix and fail their existing length checks rather than treating it as frame text. This change adds no capability negotiation or silent downgrade.

The frame-local contract detects self-inconsistency and accidental corruption. It does not authenticate an untrusted sender's claimed palette; transport identity and encryption retain their separate responsibilities. Terminal-command safety and bounds are enforced independently of metadata claims.

## Presentation and recovery

Native terminal receivers compare validated geometry with effective render dimensions and the presenter's cached physical viewport. A valid oversized frame from an earlier resize/settings state is skipped, counted separately and does not close the connection or block audio. A later fitting frame resumes normally. The existing presenter retains responsibility for overlays and clipping at the actual write-time resize boundary. Snapshot/file output uses effective configured dimensions when there is no terminal viewport.

Malformed frames return ERROR_NETWORK_PROTOCOL before presentation side effects. The connection owner decides whether to close/reconnect; the validator itself retains no poisoned parsing state. Browser frame rejection follows the existing dropped-frame error path. Trusted local output, including Kitty image export, never passes through this network validator.
