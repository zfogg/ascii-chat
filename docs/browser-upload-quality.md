# Browser upload quality

Compressed browser uploads use local transport feedback rather than a fixed
`width * height * 2 * fps` bitrate. `UploadQuality` bounds the initial target to
64 kbit/s–8 Mbit/s at 0.12 bits per source pixel per frame. Requested capture
dimensions and FPS remain ceilings; this controller never changes the camera
track, source selection, or user settings. Raw uploads and local browser-host
input retain their existing paths.

Every 500 ms, queued bytes above 64 KiB, queued video older than 60 ms, or a
new outbound drop reduce the network target by 35%, down to 4% (with a final
32 kbit/s floor). Encoder backlog instead reduces the compute target by 20%,
down to 20%, without treating a busy encoder as a slow network. The lower target
selects spatial tiers of 100%, 75%, 50%, or 25%, and reduces FPS down to 5 (or
the requested ceiling when lower). Five healthy seconds restore five percentage
points, so recovery cannot immediately refill a recently congested link.

The encoder scales frames internally and recreates/probes the exact Annex B
configuration when quality changes. Generation checks reject old output; each
configuration begins with a keyframe and reset flag. Existing outbound queue
admission, audio/control reserves, priority/barriers, 100 ms application video
expiry, and keyframe recovery remain authoritative. Feedback is collected before
capture backpressure can skip a frame, including after WebRTC host promotion.

This policy belongs beside WebCodecs: browser capture frames and JavaScript
transport queues do not traverse the C encoder. Moving the policy into C would
add a WASM call boundary without sharing a current native encoding path. No wire
format or native encoder behavior changes are needed.

## Evidence

From `web/web`:

```
pnpm exec vp test run tests/network/upload-quality.test.ts tests/network/h265-encoder.test.ts tests/network/outbound-queue.test.ts
pnpm exec playwright test --config playwright.adaptive.config.ts
```

The browser test requires installed Chrome with H.264 WebCodecs support. It uses
real encoding, decoding, and a visible received canvas, with an in-process shaped
ordered link (3 Mbit/s, then 120 kbit/s for ten seconds, then recovery). Synthetic
20 ms audio packets share the actual `OutboundQueue`. The test records quality,
decoded frame age, audio packet count, and maximum receive gap in
`test-results/*/collapse-recovery.json`, plus `adaptive-upload.png`. This proves
codec/queue behavior, not microphone playback, native networking, or physical
network shaping. Full ceiling recovery is covered with deterministic policy time;
the 34-second browser scenario checks gradual partial recovery.

An already submitted reliable transport packet cannot be recalled. Sudden
collapse can therefore delay audio behind that packet even though queued audio
has priority over subsequent video. The test bounds and reports this transient
gap rather than claiming zero delay or end-to-end bandwidth estimation.
