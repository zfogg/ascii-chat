# Closed pull request review audit — 2026-11-06

Scope: pull requests closed from 2026-08-06 through 2026-11-06 in this repository. The inventory contains 132 closed PRs, 587 review threads, and 205 threads still marked unresolved. Each row below links to the first comment in one such thread. A GitHub thread being unresolved does not establish that the reported defect is still present.

The PR includes fixes for live findings; other entries were checked against the current code or GitHub code-scanning state. The original closed-PR threads remain unchanged while this draft is reviewed.

Hosted MCP URL rendering is **not** an SSRF sandbox. Its existing [deployment guidance](mcp.md) requires external egress controls for private, loopback, link-local and metadata networks, including redirects and nested media requests. Operators must enforce those controls; the application does not do so.

The Windows error-popup screenshot smoke test failed because the captured backtrace did not contain the expected symbol; its error-journal contract passed. The mirror lazy-audio integration rerun also failed while looking for a debug-memory row. These failures are not counted as passing validation.

## Fixed in this PR (89)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #452 | [discussion](https://github.com/zfogg/ascii-chat/pull/452#discussion_r4188521092) | `lib/audio/visualization.c` |
| #453 | [discussion](https://github.com/zfogg/ascii-chat/pull/453#discussion_r4188613597) | `lib/audio/visualization.c` |
| #466 | [discussion](https://github.com/zfogg/ascii-chat/pull/466#discussion_r4236342019) | `lib/stats/runtime.c` |
| #466 | [discussion](https://github.com/zfogg/ascii-chat/pull/466#discussion_r4236342031) | `lib/stats/runtime.c` |
| #466 | [discussion](https://github.com/zfogg/ascii-chat/pull/466#discussion_r4236342038) | `lib/stats/runtime.c` |
| #466 | [discussion](https://github.com/zfogg/ascii-chat/pull/466#discussion_r4236342043) | `lib/stats/runtime.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683746) | `include/ascii-chat/options/options.h` — Header-defined defaults are used selectively by registry translation units. |
| #469 | [discussion](https://github.com/zfogg/ascii-chat/pull/469#discussion_r4236253471) | `lib/crypto/key_identity/display.c` |
| #470 | [discussion](https://github.com/zfogg/ascii-chat/pull/470#discussion_r4236272607) | `lib/stats/runtime.c` |
| #470 | [discussion](https://github.com/zfogg/ascii-chat/pull/470#discussion_r4236272609) | `lib/stats/runtime.c` |
| #470 | [discussion](https://github.com/zfogg/ascii-chat/pull/470#discussion_r4236275475) | `tests/integration/stats_tui.py` |
| #470 | [discussion](https://github.com/zfogg/ascii-chat/pull/470#discussion_r4236275482) | `tests/unit/stats/stats_test.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236328643) | `cmake/compiler/SIMD.cmake` |
| #472 | [discussion](https://github.com/zfogg/ascii-chat/pull/472#discussion_r4236379808) | `lib/ui/controller.c` |
| #472 | [discussion](https://github.com/zfogg/ascii-chat/pull/472#discussion_r4236379812) | `lib/ui/controller.c` |
| #473 | [discussion](https://github.com/zfogg/ascii-chat/pull/473#discussion_r4236917519) | `src/common/session/keyboard_handler.c` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4236742442) | `web/web/src/hooks/useWebcamStream.ts` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4236742452) | `web/web/src/wasm/client.ts` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4236742456) | `web/web/tests/e2e/video-encoding.spec.ts` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4237072656) | `web/web/src/wasm/dist/manifest.json` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4237072660) | `web/web/src/components/DeviceSetupModal.tsx` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4237384702) | `lib/CMakeLists.txt` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4241158448) | `tests/audio/analyze_network_audio.py` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4241158455) | `tests/audio/test_network_audio.py` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4241158457) | `tests/audio/test_network_audio.py` |
| #484 | [discussion](https://github.com/zfogg/ascii-chat/pull/484#discussion_r4241316677) | `lib/options/registry/audio.c` |
| #484 | [discussion](https://github.com/zfogg/ascii-chat/pull/484#discussion_r4241316683) | `docs/audio-spectral.md` |
| #486 | [discussion](https://github.com/zfogg/ascii-chat/pull/486#discussion_r4241363507) | `src/common/session/display.c` |
| #543 | [discussion](https://github.com/zfogg/ascii-chat/pull/543#discussion_r4339771296) | `lib/options/options.c` |
| #543 | [discussion](https://github.com/zfogg/ascii-chat/pull/543#discussion_r4339771302) | `lib/options/options.c` |
| #548 | [discussion](https://github.com/zfogg/ascii-chat/pull/548#discussion_r4343087003) | `lib/ui/mdns.c` |
| #548 | [discussion](https://github.com/zfogg/ascii-chat/pull/548#discussion_r4343765678) | `tests/integration/mdns_terminal.py` |
| #552 | [discussion](https://github.com/zfogg/ascii-chat/pull/552#discussion_r4343138966) | `web/web/src/components/shared/BinarySettings.tsx` |
| #552 | [discussion](https://github.com/zfogg/ascii-chat/pull/552#discussion_r4343138976) | `web/web/src/components/shared/BinarySettings.tsx` |
| #552 | [discussion](https://github.com/zfogg/ascii-chat/pull/552#discussion_r4343423504) | `web/web/src/components/shared/BinarySettings.tsx` |
| #552 | [discussion](https://github.com/zfogg/ascii-chat/pull/552#discussion_r4343423511) | `web/web/src/components/shared/BinarySettings.tsx` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4343897097) | `web/web/src/components/ErrorOverlay.tsx` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4343897106) | `web/web/src/wasm/client.ts` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4344153356) | `lib/network/connection.c` |
| #557 | [discussion](https://github.com/zfogg/ascii-chat/pull/557#discussion_r4346318146) | `tests/integration/symbolizer_offline.py` |
| #557 | [discussion](https://github.com/zfogg/ascii-chat/pull/557#discussion_r4346318153) | `tests/CMakeLists.txt` |
| #561 | [discussion](https://github.com/zfogg/ascii-chat/pull/561#discussion_r4345019758) | `lib/ui/session_help.c` |
| #562 | [discussion](https://github.com/zfogg/ascii-chat/pull/562#discussion_r4346403854) | `tests/integration/errno_tui_ellipsis.py` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527194) | `docs/telemetry.md` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527202) | `lib/network/http_client.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527207) | `lib/network/http_client.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527211) | `lib/otel.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527221) | `src/client/audio.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349246068) | `lib/audio/audio.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349246085) | `lib/discovery/database.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349246095) | `lib/otel.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349718466) | `lib/media/source.c` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363413101) | `lib/asciichat_errno.c` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363449249) | `lib/asciichat_errno.c` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363449259) | `lib/tui/controller.c` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363449269) | `lib/tui/error_popup.c` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363449275) | `tests/integration/error_popup.py` |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4364049883) | `tests/integration/source_paths.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365898032) | `src/mcp-server/protocol.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915423) | `lib/options/config/presets.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915428) | `lib/options/manpage.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915432) | `lib/options/registry/mode_defaults.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915437) | `src/mcp-server/stdio.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915446) | `tests/integration/test_mcp_render_url.py` |
| #580 | [discussion](https://github.com/zfogg/ascii-chat/pull/580#discussion_r4373114810) | `lib/media/source.c` |
| #598 | [discussion](https://github.com/zfogg/ascii-chat/pull/598#discussion_r4448845021) | `web/web/src/network/WebRTCSession.ts` |
| #598 | [discussion](https://github.com/zfogg/ascii-chat/pull/598#discussion_r4448845023) | `web/web/src/network/BrowserHostSession.ts` |
| #603 | [discussion](https://github.com/zfogg/ascii-chat/pull/603#discussion_r4449751615) | `.github/workflows/test.yml` |
| #603 | [discussion](https://github.com/zfogg/ascii-chat/pull/603#discussion_r4449838135) | `lib/media/ffmpeg_decoder.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450120820) | `lib/audio/mixer.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259703) | `lib/audio/processor.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259711) | `lib/audio/recording.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259718) | `lib/video/ascii/common.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259724) | `web/packages/shared/src/components/DeviceSetupModal.tsx` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259732) | `web/packages/shared/src/components/DeviceSetupModal.tsx` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259743) | `web/packages/shared/src/components/DeviceSetupModal.tsx` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259753) | `web/web/src/audio/AudioPipeline.ts` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259763) | `web/web/src/audio/AudioPipeline.ts` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259768) | `web/web/src/audio/AudioPipeline.ts` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450437543) | `lib/audio/mixer.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450437553) | `src/client/audio.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450437557) | `web/web/src/audio/capture.worker.ts` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4450985999) | `tests/integration/discovery_audio.py` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4450986014) | `tests/integration/synthetic_audio_sessions.py` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4450986021) | `tests/integration/synthetic_audio_sessions.py` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4453227152) | `lib/log/json.c` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4453227169) | `tests/contracts/session/host_render_test.c` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4453227180) | `tests/contracts/session/participant_workers_test.c` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4453227198) | `tests/integration/panic_report.py` |

## Already addressed in current code (41)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #452 | [discussion](https://github.com/zfogg/ascii-chat/pull/452#discussion_r4188521073) | `lib/options/registry/display.c` |
| #452 | [discussion](https://github.com/zfogg/ascii-chat/pull/452#discussion_r4188521081) | `src/common/session/display.c` |
| #456 | [discussion](https://github.com/zfogg/ascii-chat/pull/456#discussion_r4230294133) | `src/server/main.c` |
| #456 | [discussion](https://github.com/zfogg/ascii-chat/pull/456#discussion_r4230294146) | `src/common/session/server_like.c` |
| #461 | [discussion](https://github.com/zfogg/ascii-chat/pull/461#discussion_r4231440262) | `lib/options/config/config.c` |
| #461 | [discussion](https://github.com/zfogg/ascii-chat/pull/461#discussion_r4231440278) | `lib/video/anim/test_pattern.c` |
| #462 | [discussion](https://github.com/zfogg/ascii-chat/pull/462#discussion_r4235305470) | `lib/ui/status.c` |
| #463 | [discussion](https://github.com/zfogg/ascii-chat/pull/463#discussion_r4235352372) | `tests/integration/fps_counter.py` |
| #464 | [discussion](https://github.com/zfogg/ascii-chat/pull/464#discussion_r4235373701) | `lib/media/ffmpeg_encoder.c` |
| #465 | [discussion](https://github.com/zfogg/ascii-chat/pull/465#discussion_r4235395909) | `lib/options/options.c` |
| #465 | [discussion](https://github.com/zfogg/ascii-chat/pull/465#discussion_r4235395913) | `lib/platform/windows/question.c` |
| #467 | [discussion](https://github.com/zfogg/ascii-chat/pull/467#discussion_r4235697955) | `lib/log/log.c` |
| #467 | [discussion](https://github.com/zfogg/ascii-chat/pull/467#discussion_r4235697956) | `src/server/main.c` |
| #467 | [discussion](https://github.com/zfogg/ascii-chat/pull/467#discussion_r4236032125) | `lib/log/log.c` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4237384699) | `lib/CMakeLists.txt` |
| #481 | [discussion](https://github.com/zfogg/ascii-chat/pull/481#discussion_r4240429644) | `tests/integration/client_like_lifecycle.c` |
| #483 | [discussion](https://github.com/zfogg/ascii-chat/pull/483#discussion_r4240399247) | `lib/video/h265/encoder.c` |
| #486 | [discussion](https://github.com/zfogg/ascii-chat/pull/486#discussion_r4241280149) | `src/common/session/display.c` |
| #486 | [discussion](https://github.com/zfogg/ascii-chat/pull/486#discussion_r4241280154) | `lib/video/ascii/zalgo.c` |
| #486 | [discussion](https://github.com/zfogg/ascii-chat/pull/486#discussion_r4241280158) | `lib/options/registry/display.c` |
| #488 | [discussion](https://github.com/zfogg/ascii-chat/pull/488#discussion_r4241324810) | `src/client/audio.c` |
| #488 | [discussion](https://github.com/zfogg/ascii-chat/pull/488#discussion_r4241324813) | `src/client/audio.c` |
| #489 | [discussion](https://github.com/zfogg/ascii-chat/pull/489#discussion_r4241482535) | `tests/CMakeLists.txt` |
| #489 | [discussion](https://github.com/zfogg/ascii-chat/pull/489#discussion_r4241490156) | `src/server/client.c` |
| #489 | [discussion](https://github.com/zfogg/ascii-chat/pull/489#discussion_r4241490160) | `tests/CMakeLists.txt` |
| #490 | [discussion](https://github.com/zfogg/ascii-chat/pull/490#discussion_r4242464274) | `src/server/crypto.c` |
| #548 | [discussion](https://github.com/zfogg/ascii-chat/pull/548#discussion_r4343765671) | `lib/network/mdns/mdns.c` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4343880874) | `web/web/src/network/ClientConnection.ts` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4343880876) | `lib/network/packet/packet.c` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4343880877) | `web/web/src/network/ClientConnection.ts` |
| #553 | [discussion](https://github.com/zfogg/ascii-chat/pull/553#discussion_r4344153364) | `lib/network/packet/packet.c` |
| #558 | [discussion](https://github.com/zfogg/ascii-chat/pull/558#discussion_r4344411073) | `lib/network/acip/send.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527236) | `src/client/audio.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349246105) | `lib/stats/runtime.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365898029) | `include/ascii-chat/options/options.h` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365915434) | `src/main.c` |
| #580 | [discussion](https://github.com/zfogg/ascii-chat/pull/580#discussion_r4371602368) | `lib/media/source.c` |
| #580 | [discussion](https://github.com/zfogg/ascii-chat/pull/580#discussion_r4372932663) | `.github/workflows/test.yml` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259685) | `lib/audio/audio.c` |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450259695) | `lib/audio/client_pipeline.cpp` |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4450985985) | `src/common/session/consensus.c` |

## Code scanning reports the alert fixed (26)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #452 | [discussion](https://github.com/zfogg/ascii-chat/pull/452#discussion_r4188526233) | `lib/audio/visualization.c` |
| #452 | [discussion](https://github.com/zfogg/ascii-chat/pull/452#discussion_r4188526259) | `lib/audio/visualization.c` |
| #453 | [discussion](https://github.com/zfogg/ascii-chat/pull/453#discussion_r4189136463) | `lib/audio/visualization.c` |
| #460 | [discussion](https://github.com/zfogg/ascii-chat/pull/460#discussion_r4235278532) | `lib/video/ascii/scalar/foreground.c` |
| #464 | [discussion](https://github.com/zfogg/ascii-chat/pull/464#discussion_r4236048388) | `lib/ui/render_progress.c` |
| #466 | [discussion](https://github.com/zfogg/ascii-chat/pull/466#discussion_r4236342048) | `lib/stats/runtime.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683718) | `lib/debug/sync.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683719) | `lib/network/packet/packet.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683721) | `lib/debug/sync.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683728) | `lib/debug/stats.c` |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683740) | `lib/debug/errno.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236507955) | `lib/video/ascii/simd/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236507974) | `lib/video/ascii/simd/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236507986) | `lib/video/ascii/avx2/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236507997) | `lib/video/ascii/neon/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236508007) | `lib/video/ascii/sse2/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236508017) | `lib/video/ascii/ssse3/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4236508025) | `lib/video/ascii/sve/luminance.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4237133519) | `lib/debug/errno.c` |
| #471 | [discussion](https://github.com/zfogg/ascii-chat/pull/471#discussion_r4237133523) | `lib/debug/stats.c` |
| #472 | [discussion](https://github.com/zfogg/ascii-chat/pull/472#discussion_r4236974040) | `lib/debug/mutex.c` |
| #474 | [discussion](https://github.com/zfogg/ascii-chat/pull/474#discussion_r4237295151) | `lib/platform/wasm/stubs/crypto.c` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4237438389) | `lib/audio/spectral.c` |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4237438397) | `lib/audio/client_pipeline.cpp` |
| #480 | [discussion](https://github.com/zfogg/ascii-chat/pull/480#discussion_r4237451107) | `lib/network/tcp/transport.c` |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365911072) | `src/mcp-server/http.c` |

## Assessed; no code change (44)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #458 | [discussion](https://github.com/zfogg/ascii-chat/pull/458#discussion_r4230644356) | `lib/platform/macos/process_title.c` — The macOS build uses the one-argument pthread_setname_np signature. |
| #458 | [discussion](https://github.com/zfogg/ascii-chat/pull/458#discussion_r4230644389) | `lib/platform/macos/process_title.c` — CFRelease is a platform macro; these calls release Core Foundation objects. |
| #458 | [discussion](https://github.com/zfogg/ascii-chat/pull/458#discussion_r4230644406) | `lib/platform/macos/process_title.c` — CFRelease is a platform macro; these calls release Core Foundation objects. |
| #460 | [discussion](https://github.com/zfogg/ascii-chat/pull/460#discussion_r4235278523) | `lib/video/ascii/ascii.c` — Integer constants are valid C arguments to size_t parameters. |
| #460 | [discussion](https://github.com/zfogg/ascii-chat/pull/460#discussion_r4235298752) | `lib/video/ascii/ascii.c` — Another live screen owns the terminal; forcing a cursor escape would override that screen. The exit hook shuts down the controller and restores the cursor. |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683711) | `lib/options/options.c` — Integer constants are valid C arguments to size_t parameters. |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683712) | `lib/options/options.c` — Integer constants are valid C arguments to size_t parameters. |
| #468 | [discussion](https://github.com/zfogg/ascii-chat/pull/468#discussion_r4236683716) | `lib/options/options.c` — Integer constants are valid C arguments to size_t parameters. |
| #469 | [discussion](https://github.com/zfogg/ascii-chat/pull/469#discussion_r4236556764) | `lib/crypto/key_identity/keymask.c` — Integer constants are valid C arguments to size_t parameters. |
| #472 | [discussion](https://github.com/zfogg/ascii-chat/pull/472#discussion_r4236974034) | `lib/debug/named.c` — The bounds check remains necessary for 32-bit size_t. |
| #486 | [discussion](https://github.com/zfogg/ascii-chat/pull/486#discussion_r4241538520) | `lib/video/ascii/zalgo.c` — The explicit increment consumes a second byte of a validated multibyte or escape sequence. |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348518754) | `lib/options/config/schema.c` — Integer constants are valid C arguments to size_t parameters. |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348518801) | `lib/options/options.c` — Integer constants are valid C arguments to size_t parameters. |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363428446) | `include/ascii-chat/common/error_codes.h` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #574 | [discussion](https://github.com/zfogg/ascii-chat/pull/574#discussion_r4363428493) | `lib/tui/error_popup.c` — The explicit increment consumes a second byte of a validated multibyte or escape sequence. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365911075) | `src/mcp-server/http.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4365911080) | `src/mcp-server/http.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4366505519) | `src/mcp-server/request.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4366505530) | `src/mcp-server/protocol.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4366505550) | `src/mcp-server/request.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4366505551) | `src/main.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4366505555) | `lib/options/json.c` — Maintainability heuristic; current code is deliberately structured as a protocol handler or compound validation and no functional defect was identified. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4369552617) | `src/mcp-server/request.c` — Integer constants are valid C arguments to size_t parameters. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4369552620) | `src/mcp-server/request.c` — Integer constants are valid C arguments to size_t parameters. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4369552623) | `src/mcp-server/request.c` — Integer constants are valid C arguments to size_t parameters. |
| #578 | [discussion](https://github.com/zfogg/ascii-chat/pull/578#discussion_r4369552627) | `src/mcp-server/request.c` — Integer constants are valid C arguments to size_t parameters. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147474) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147483) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147488) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147490) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147498) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147506) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147512) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147516) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147529) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147539) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147569) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4430147581) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4434100086) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #581 | [discussion](https://github.com/zfogg/ascii-chat/pull/581#discussion_r4434100093) | `lib/platform/windows/keyboard.c` — Windows keyboard switch has distinct VK_* cases; the reported branches and state variables are used in the Windows build. |
| #588 | [discussion](https://github.com/zfogg/ascii-chat/pull/588#discussion_r4437763397) | `src/discovery/session.c` — Integer constants are valid C arguments to size_t parameters. |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450175940) | `lib/audio/opus.c` — The apparent declarations are arguments to the Opus decoder call; the output parameters are used. |
| #605 | [discussion](https://github.com/zfogg/ascii-chat/pull/605#discussion_r4450175956) | `lib/audio/opus.c` — The apparent declarations are arguments to the Opus decoder call; the output parameters are used. |
| #606 | [discussion](https://github.com/zfogg/ascii-chat/pull/606#discussion_r4452104483) | `src/session/h265/client.c` — The bounds check remains necessary for 32-bit size_t. |

## Not applicable (4)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #478 | [discussion](https://github.com/zfogg/ascii-chat/pull/478#discussion_r4241158426) | `lib/audio/CMakeLists.txt` |
| #489 | [discussion](https://github.com/zfogg/ascii-chat/pull/489#discussion_r4241490154) | `lib/platform/posix/keyboard.c` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4348527246) | `tests/integration/test_otel.py` |
| #567 | [discussion](https://github.com/zfogg/ascii-chat/pull/567#discussion_r4349718481) | `tests/CMakeLists.txt` |

## Declined by the user (1)

| PR | Comment | Current path / assessment |
| --- | --- | --- |
| #454 | [discussion](https://github.com/zfogg/ascii-chat/pull/454#discussion_r4188967774) | `web/www/public/ascii-chat-man1.html` — User declined the manpage edit. |

## Review-body findings

- [#460 rainbow output](https://github.com/zfogg/ascii-chat/pull/460#pullrequestreview-5475742054) and [#489 key-byte logging](https://github.com/zfogg/ascii-chat/pull/489#pullrequestreview-5483221075): fixed in this PR.
- [#530 encrypted packet CRC](https://github.com/zfogg/ascii-chat/pull/530#pullrequestreview-5578262550): addressed in the [earlier audit](closed-pr-review-audit-2026-10-19.md).
- [#526 host packet accounting](https://github.com/zfogg/ascii-chat/pull/526#issuecomment-6272853120): the earlier follow-up removed the duplicate count and recorded it in the same audit.
- [#529 keyboard cleanup](https://github.com/zfogg/ascii-chat/pull/529#pullrequestreview-5579860100): keyboard ownership is shared with the required sync screen; unconditional destruction here could disable that screen. Process shutdown restores the keyboard. A separate ownership redesign would be needed for safe early release.
- [#564 update cache](https://github.com/zfogg/ascii-chat/pull/564#pullrequestreview-5613483935): path joining is already corrected; helper probing was moved before presentation and its cache made atomic in this PR. The [formatting report](https://github.com/zfogg/ascii-chat/pull/564#pullrequestreview-5615043439) was checked against current formatting.
- [#565 log selection and highlighting](https://github.com/zfogg/ascii-chat/pull/565#pullrequestreview-5612614575): current log rendering retains the selected position by sequence and highlights matching text.
- [#567 early telemetry options](https://github.com/zfogg/ascii-chat/pull/567#pullrequestreview-5614436138): fixed in this PR by using the option registry during early mode detection.
- [#578 TOML mode validation](https://github.com/zfogg/ascii-chat/pull/578#pullrequestreview-5636737864) and [non-PCH header](https://github.com/zfogg/ascii-chat/pull/578#pullrequestreview-5636779836): already present in current code. Its [hosted URL and bind-address review](https://github.com/zfogg/ascii-chat/pull/578#pullrequestreview-5641365518) is addressed by the existing explicit network-isolation requirement and the bind validation in this PR; the application does not itself enforce that egress policy.
- [#581 word deletion](https://github.com/zfogg/ascii-chat/pull/581#pullrequestreview-5720877427): current editor uses an ASCII-only whitespace predicate.

## Nitpicks in review bodies

- [#448 regex fallback](https://github.com/zfogg/ascii-chat/pull/448#pullrequestreview-5409092425): the fallback probe now runs on all platforms using the explicit JIT-disable setting.
- [#459 discovery callback](https://github.com/zfogg/ascii-chat/pull/459#pullrequestreview-5470865421): current code copies the error fields under the mutex.
- [#473 video restoration](https://github.com/zfogg/ascii-chat/pull/473#pullrequestreview-5477698500): the integration predicate now requires a dense, multicolored video frame. A separate real Windows video capture satisfied the predicate; the full script failed earlier in its disabled-audio setup.
- [#474 decoded-frame copy](https://github.com/zfogg/ascii-chat/pull/474#pullrequestreview-5477993598): retained the padded, aligned FFmpeg conversion buffer and compact row copy. Direct writes would need separately validated slot capacity and FFmpeg alignment across frame widths.
