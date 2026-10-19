# Closed PR review audit — October 16–19, 2026

Reviewed all 28 PRs closed in this interval, including inline threads, review summaries, and conversation comments. Coverage reports and test-result notifications were excluded. The following 57 threads were unresolved at the start. Each disposition was checked against current source before changes.

| PR | Finding | Disposition |
| --- | --- | --- |
| #527 | [src/server/protocol.c](https://github.com/zfogg/ascii-chat/pull/527#discussion_r4317252536) | Fixed: reject runtime settings until capability bootstrap and after shutdown begins. |
| #527 | [src/server/protocol.c](https://github.com/zfogg/ascii-chat/pull/527#discussion_r4317252560) | Fixed: ACK writes and transport teardown share the send mutex; shutdown rejects late ACKs. |
| #526 | [lib/network/tcp/async.c](https://github.com/zfogg/ascii-chat/pull/526#discussion_r4316567546) | Already fixed by PR #528: failed stream startup returns NULL with deferred destruction. |
| #525 | [web/web/tests/e2e/face-follow.spec.ts](https://github.com/zfogg/ascii-chat/pull/525#discussion_r4316188268) | Fixed: assert source crop aspect ratio. |
| #525 | [lib/video/face_follow.c](https://github.com/zfogg/ascii-chat/pull/525#discussion_r4316190590) | Fixed: initialize the first detected crop for zero-delay snapshots. |
| #525 | [lib/video/face_detect.cpp](https://github.com/zfogg/ascii-chat/pull/525#discussion_r4316246638) | Clarified: parse the documented byte layout before interpreting 16-short detection records. |
| #524 | [src/common/session/host.c](https://github.com/zfogg/ascii-chat/pull/524#discussion_r4316064511) | Fixed: split render-target validation into named backend predicates. |
| #523 | [src/common/session/host.c](https://github.com/zfogg/ascii-chat/pull/523#discussion_r4315425468) | Fixed: resolve half-block mode to foreground for viewers without UTF-8. |
| #523 | [src/common/session/host.c](https://github.com/zfogg/ascii-chat/pull/523#discussion_r4315425512) | Fixed: fall back to the standard ASCII palette for non-UTF-8 local rendering. |
| #522 | [.github/workflows/web-tests.yml](https://github.com/zfogg/ascii-chat/pull/522#discussion_r4315477109) | Fixed: disable checkout credential persistence. |
| #520 | [web/web/tests/e2e/camera-cadence.spec.ts](https://github.com/zfogg/ascii-chat/pull/520#discussion_r4313252703) | Fixed: require restored uploads to remain above 17 FPS. |
| #513 | [include/ascii-chat/audio/mixer.h](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309422131) | Fixed: use a multiword source bitset for all 128 mixer slots. |
| #513 | [src/discovery-service/server.c](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309422135) | Fixed: dynamically allocate per-session handoff entries; release empty entries and shutdown state. |
| #513 | [web/web/src/network/BrowserHostSession.ts](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309422138) | Fixed: stop obsolete browser hosting on a successor commit. |
| #513 | [lib/network/consensus/handoff.c](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309422142) | Fixed: clear rejected candidates after a fresh measurement or committed handoff. |
| #513 | [src/discovery/session.c](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309477272) | Fixed: re-dispatch native hosts on pending handoff and destroy the old host on demotion. |
| #513 | [lib/network/consensus/handoff.c](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309477278) | Fixed: mark incumbent loss independently of measurement membership. |
| #513 | [web/web/src/network/WebRTCSession.ts](https://github.com/zfogg/ascii-chat/pull/513#discussion_r4309477281) | Already fixed: WebRTCSession.closePeer closes and nulls peerCrypto before replacement. |
| #512 | [lib/options/options.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984198) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/options/options.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984209) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/avx2/luminance.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984216) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/render.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984220) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/render.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984230) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/render.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984235) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/sgr.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984241) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/sgr.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984254) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/sgr.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984260) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/sgr.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984271) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/sse2/luminance.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984280) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/ssse3/luminance.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984289) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/terminal/ansi.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984296) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/terminal/ansi.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984305) | False positive: standard integer conversions to size_t are valid; intrinsic buffers are correctly declared and bounded. Native compilation verifies the supported x86 SIMD paths. |
| #512 | [lib/video/ascii/neon/luminance.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984314) | Fixed: rename the NEON vector local to avoid hiding rgb. |
| #512 | [lib/video/ascii/simd/quantize.c](https://github.com/zfogg/ascii-chat/pull/512#discussion_r4309984318) | Fixed: mark the per-block active count used on fixed-width SIMD builds. |
| #511 | [src/client/audio.c](https://github.com/zfogg/ascii-chat/pull/511#discussion_r4309290674) | Already fixed: capture dump writers are opened in lifecycle-guarded audio_client_init, not each capture thread. |
| #497 | [web/web/src/network/ClientConnection.ts](https://github.com/zfogg/ascii-chat/pull/497#discussion_r4300382857) | Fixed: preserve terminal ERROR when the asynchronous socket close arrives, including reconnect failures. |
| #495 | [web/web/src/pages/Mirror.tsx](https://github.com/zfogg/ascii-chat/pull/495#discussion_r4300340917) | Already fixed: useMirrorRenderLoop connects file media to captureVisualizationAudio. |
| #495 | [web/web/src/pages/Mirror.tsx](https://github.com/zfogg/ascii-chat/pull/495#discussion_r4300340924) | Already fixed: Mirror keeps mediaFlipX/mediaFlipY separate from camera preferences. |
| #495 | [web/web/src/hooks/useMirrorFile.ts](https://github.com/zfogg/ascii-chat/pull/495#discussion_r4300340927) | Already fixed: streamMedia and transcodeMedia probe audio codec support before accepting native playback. |
| #494 | [web/web/src/network/BrowserHostSession.ts](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300304147) | Fixed: signaling reconnect terminates obsolete hosting, including adopted hosts. |
| #494 | [web/web/src/network/BrowserHostSession.ts](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300304153) | Fixed: validate ICE configuration before starting/publishing the hosted session. |
| #494 | [src/web/browser_host.c](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300331280) | Fixed: run the shared 20 ms Opus mix step from browser scheduling and initialize codecs before receiving audio. |
| #494 | [web/web/src/network/BrowserVideoDecoder.ts](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300331283) | Not applicable to current native discovery: src/discovery/main.c sends raw RGB via acip_send_image_frame; src/client/capture.c is the fixed-server client, not the browser-host discovery path. |
| #494 | [web/web/src/components/SessionInvite.tsx](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300331287) | Fixed: copied invitations preserve the custom TURN username and keep its password in the URL fragment. |
| #494 | [web/web/tests/network/BrowserVideoDecoder.test.ts](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300331292) | Fixed: import test helpers from vite-plus/test. |
| #494 | [lib/network/acip/acds_client.c](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300418611) | False positive: valid_name bounds the uint8_t length; memcpy safely promotes it to size_t. |
| #494 | [lib/network/acip/acds_client.c](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300418626) | False positive: the bounded uint8_t length safely promotes to memcpy size_t. |
| #494 | [lib/network/acip/acds_client.c](https://github.com/zfogg/ascii-chat/pull/494#discussion_r4300418639) | False positive: the bounded uint8_t length safely promotes to memcpy size_t. |
| #493 | [web/www/e2e/demo-widget.spec.ts](https://github.com/zfogg/ascii-chat/pull/493#discussion_r4300284233) | Fixed: skip dev-only module instrumentation when the target does not serve Vite client. |
| #491 | [src/web/common/ui.c](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299840532) | Fixed: synchronize active speaker/microphone state into C when the renderer is ready. |
| #491 | [web/web/src/audio/AudioPipeline.ts](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299840538) | Fixed: seed playback gain from the current C speakers_volume before enabling playback. |
| #491 | [web/web/src/pages/Mirror.tsx](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299840548) | Already fixed: Mirror Escape calls stopPlayback, clearing persisted playing state. |
| #491 | [lib/ui/session_help.c](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299849170) | Fixed: remove unreachable content-width clamp after max_width >= 6. |
| #491 | [lib/ui/controller.c](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299964679) | Intentional: help displays the live presentation FPS counter; periodic refresh keeps that line current. |
| #491 | [web/packages/shared/src/components/AsciiRenderer/useAsciiRendererHandle.ts](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299964689) | Fixed: preserve writeFrame boolean failure contract when C rejects a frame or allocation fails. |
| #491 | [web/web/src/pages/Client.tsx](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299964693) | Fixed: seed each playback activation from C volume. |
| #491 | [web/web/src/pages/Mirror.tsx](https://github.com/zfogg/ascii-chat/pull/491#discussion_r4299964697) | Already fixed: Mirror Escape uses the complete stopPlayback path. |

Additional findings in review summaries:

- #526: remove duplicate inbound packet accounting from the native host receive loop.
- #527: expose options_retired_snapshot_count() and the WASM _wasm_options_retired_count() export for the documented borrowed-snapshot retention tradeoff. Existing no-op patch suppression already coalesces unchanged settings. Reclamation cannot invalidate the public until-shutdown borrow guarantee.
- #525: pin and SHA-256-check the moving-face source clip, including existing cached input; give Face Follow options their own defaults.
- #521: test supported camera-track preference, cleanup, failure, and recovery.
- #518: the newer runtime-settings browser regression directly checks the acknowledged matrix_rain value; its acceptance coverage supersedes the older URL-only check.
- #505: remove the second CRC pass after the same WebRTC packet was already verified.
- #527/#491 validation: preserve C observer notifications across intervening snapshot reads and acknowledge observed settings before React echoes them back.
- #527/#491 validation: query font data explicitly without changing animation options; asynchronous renderer recreation must not restore an obsolete Matrix selection.
- Follow-up review: each participant owns its Opus decoder and output encoder; mix-minus excludes the recipient before clipping. Compiled tests verify sender silence and stream isolation. Face Follow default constants now live in their consuming registries.

Validation results are recorded in the accompanying PR description.
