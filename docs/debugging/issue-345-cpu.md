# Issue 345: native CPU investigation

2026-10-09 · https://github.com/zfogg/ascii-chat/issues/345

Baseline: `fd5c55eefcd2b5b916a2111b70d88bb5bf8fe4e1`. Windows Debug and Linux Docker Debug/ASan builds.

## Findings and fixes

**A closed stdout pipe reproduced the busy orphan on both platforms.** The Windows client used 121.25% CPU; Linux used 142.35%. Windows mirror used 103.73%. `platform_write_all()` retried permanent write failures 1,000 times per frame, logging each failure without sleeping. Logging also uses this writer, so a broken logging descriptor could recurse. The display discarded the failure and kept going.

The fix returns immediately on permanent errors, retains bounded retries for EINTR/EAGAIN, avoids logging from the low-level writer, maps Windows WriteFile failures to errno, and requests session shutdown when piped media output fails. Frame-effect buffers are freed on the failure path.

**Forced status screens with redirected output manufactured errors every frame.** Windows measured 64.06%; Linux 80.78%. A Windows debugger stack caught status rendering in `display_height` → `ansi_strip_escapes` → debug allocation/path normalization while displaying repeated keyboard-init errors. Guards now skip keyboard setup and terminal geometry queries for non-terminal descriptors. Afterward: Windows 2.19%, Linux 6.60%; 20-second follow-ups remained at 2.73% and 7.00%. A debugger-assisted 30-second baseline sample is excluded because attaching disrupted that process.

**Paused Linux playback repeatedly warned about stdin EOF at roughly 1 ms polling intervals.** EOF is normal for redirected input. It is now silent, while real keyboard read errors are rate limited. Paused redirected playback fell from 16.40% to 5.20%. This remains a polling loop; the change does not eliminate all idle overhead.

The regression failed against both original binaries because mirror was still running 10 seconds after its output reader closed. Final regression runs passed on both platforms: Windows mirror/client exited in 0.14/0.22 s, Linux in 0.21/0.26 s. Forced status output and paused EOF checks also passed. An independent run of the reusable CPU harness confirmed the connected closed-output client exits on both platforms.

## Method

CPU is process CPU time divided by elapsed wall time, multiplied by 100: **100% is one logical core**, not the whole machine. Windows uses GetProcessTimes; Linux uses `/proc/PID/stat`. These are diagnostic Debug builds, not Release benchmarks. Absolute Windows/Linux percentages are not equivalent hardware/build comparisons.

The screening matrix used two seconds of warmup followed by five one-second samples, plus a one-second server startup allowance. Short samples can include handshake/shutdown time. Longer status checks and a final closed-output run with five-second warmup confirmed the main findings. Some independent probes overlapped, so small percentage differences are not statistically meaningful. Media was synthetic or repository fixtures, usually 80×24 at 60 FPS with audio disabled unless under test. Output normally went to a null device. Harness-created processes were bounded and reaped.

Calls used `--no-check-update`, warning-level logs, and separate log paths. Encrypted loopback tests used the existing `ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1` testing switch, avoiding modifications to known_hosts. Initial runs without it failed writing known_hosts and were excluded from active-call comparisons. No public deployment or issue comment was made. The Mac checkout was older and extensively dirty, so it was not used. Concurrent unrelated local edits were preserved; these measurements are not an isolated performance benchmark of the patch.

An exited process with 0% CPU is **not** an idle pass. Active rendering can legitimately exceed one core: with four clients, Windows/Linux servers used about 198%/214%. Reducing the connected client to 1 FPS reduced it to about 1–2%. Client `--pause` pauses local media, but the server can still produce/display frames at the configured rate.

## Remaining issues and limits

- Plaintext `--no-encrypt` calls hit `CLIENT_CAPABILITIES payload size 131072 (expected 168)` and disconnect/retry. Their low CPU is not successful plaintext streaming.
- One Linux audio/reconnection run failed with ASan heap-use-after-free: `debug_atomic_ptr_format_timing` → `atomic_ptr_iter_callback` → `named_registry_for_each` → `debug_sync_print_state` → `crypto_handshake_init`. The freed object came from `packet_queue_destroy` during client cleanup. This separate debug-registry lifetime defect remains unresolved. Evidence: `build/cpu-results-linux-baseline/client-audio-server.stderr`.
- Windows HEVC initialization failed and normal clients exercised the fallback. Windows PNG render-file initialization failed because the linked FFmpeg lacks the PNG encoder. Linux PNG snapshot succeeded.
- Initial waveform/FFT cases with audio=false were rejected. Later valid audio-source variants are listed separately. `--no-audio-playback` is client/discovery-only and rejected in mirror mode.
- One Windows 144-FPS mirror sample reported 0% while alive; the repeat measured 29.38%. The initial zero is inconclusive, not an efficient idle result.
- PTY detach still leaves mirror capture running (about 30% on Linux), but terminal/EOF error amplification is removed. Scripts leaving output open still leave the application alive. Infinite reconnect remains intentional unless `--reconnect-attempts 0` is selected.
- Discovery waiting-for-peer and service loss were exercised. Local preview work continues while waiting; the short Linux service-loss window remained alive. This is not a successful two-peer WebRTC call or full recovery test. WAN/STUN/TURN permutations, real cameras/microphones/speakers, identity-key variants, and every flag combination were not exhaustively tested.
- Full Criterion suite was not run; it is unsupported natively on Windows. Focused subprocess regressions and both builds passed. The existing Windows man-page step logs a default log-path validation error despite overall build success.

## Reproduce

The status keyboard guard checks stdin and the status output descriptor (stderr
when stdout is redirected). The status thread preserves stderr in that case,
so redirecting stdout alone does not disable the display or grep/Escape input.
`tests/integration/status_redirected.py` exercises normal and redirected stdout
for server using a real PTY and rendered-screen assertions.

```sh
python tests/integration/broken_output.py --binary build/bin/ascii-chat.exe
python tests/manual/cpu_usage.py --binary build/bin/ascii-chat.exe --output build/cpu-check --filter closed-output --warmup 5 --seconds 10
python tests/manual/cpu_usage.py --binary build/bin/ascii-chat.exe --output build/cpu-matrix --warmup 5 --seconds 10
```

Use the native executable path on Linux. The manual runner supports Windows/Linux and needs ports 38451–38454 free. Its waveform/FFT cases use corrected valid flags. JSONL rows preserve commands, PIDs, samples, exits, and pipe actions. Supplemental scripts and original results remain under `build/cpu_*.py` and `build/cpu-results-*` (untracked build artifacts).

## Baseline matrix

Values are CPU percentages, **server / client** for two processes, otherwise a single process. `running` means alive at the sampling deadline, not proof of successful media exchange. Server variants 0/1/2/3/4 mean default idle / no audio mixer / forced status / no encryption / no compression. Unreachable variants 5/6/7/8 mean default retries / zero retries / splash enabled / snapshot.

| Scenario | Windows CPU (state) | Linux CPU (state) |
|---|---|---|
| server-0 | 0.00 (running) | 0.40 (running) |
| server-1 | 0.31 (running) | 0.40 (running) |
| server-2 | 64.06 (running) | 80.78 (running) |
| server-3 | 0.94 (running) | 0.60 (running) |
| server-4 | 0.00 (running) | 0.40 (running) |
| unreachable-5 | 0.00 (running) | 0.40 (running) |
| unreachable-6 | 0.00 (exit 1) | 0.00 (exit 1) |
| unreachable-7 | 0.00 (running) | 13.40 (running) |
| unreachable-8 | 0.31 (exit 1) | 0.00 (exit 1) |
| client-default | 28.75 / 51.88 (running / running) | 26.59 / 91.17 (running / running) |
| client-raw | 29.69 / 52.81 (running / running) | 48.18 / 85.37 (running / running) |
| client-no-encrypt | 0.31 / 1.56 (running / running) | 2.60 / 1.80 (running / running) |
| client-no-compress | 27.19 / 51.25 (running / running) | 24.59 / 86.37 (running / running) |
| client-audio | 30.31 / 52.81 (running / running) | 2.20 / 0.80 (exit 1 / running) |
| client-fps1 | 1.25 / 1.56 (running / running) | 4.40 / 2.20 (running / running) |
| client-fps144 | 53.44 / 61.88 (running / running) | 37.39 / 120.76 (running / running) |
| client-snapshot | 0.62 / 0.94 (running / exit 0) | 0.60 / 0.00 (running / exit 0) |
| client-snapshot3 | 25.31 / 46.25 (running / exit 0) | 7.00 / 23.79 (running / exit 0) |
| client-waveform | 0.00 / 0.00 (running / exit 2) | 0.60 / 0.00 (running / exit 2) |
| client-fft | 0.00 / 0.00 (running / exit 2) | 0.40 / 0.00 (running / exit 2) |
| client-password | 32.50 / 50.31 (running / running) | 23.19 / 84.37 (running / running) |
| client-wrong-password | 5.62 / 6.56 (running / exit 62) | 0.40 / 0.00 (running / exit 62) |
| server-loss | 0.00 / 0.62 (exit 1 / running) | 0.00 / 0.40 (exit -9 / running) |
| blocked-output | 28.98 / 51.10 (running / running) | 23.99 / 83.37 (running / running) |
| closed-output | 29.38 / 121.25 (running / running) | 24.79 / 142.35 (running / running) |
| closed-input | 30.00 / 51.56 (running / running) | 23.99 / 88.57 (running / running) |
| mirror-default | 5.30 (running) | 40.99 (running) |
| mirror-fps1 | 1.25 (running) | 14.80 (running) |
| mirror-fps144 | 0.00 (running) | 76.58 (running) |
| mirror-audio | 11.88 (running) | 39.19 (running) |
| mirror-waveform | 0.00 (exit 2) | 0.00 (exit 2) |
| mirror-fft | 0.00 (exit 2) | 0.00 (exit 2) |
| mirror-snapshot | 0.00 (exit 0) | 0.00 (exit 0) |
| mirror-matrix | 21.56 (running) | 54.19 (running) |
| mirror-color | 9.69 (running) | 43.59 (running) |
| mirror-large | 13.44 (running) | 46.99 (running) |
| file-eof | 3.75 (exit 0) | 15.40 (exit 0) |
| file-loop | 5.62 (running) | 23.59 (running) |
| file-pause | 1.56 (running) | 16.40 (running) |
| file-seek | 3.75 (exit 0) | 15.20 (exit 0) |
| file-missing | 0.00 (exit 26) | 0.00 (exit 26) |
| acds | 0.00 (running) | 0.40 (running) |
| discovery-unreachable | 0.31 (exit 42) | 0.00 (exit 42) |

## After-fix checks and extended flags

| Scenario | Windows CPU (state) | Linux CPU (state) |
|---|---|---|
| server-0 | 0.00 (running) | 0.60 (running) |
| server-2 | 2.19 (running) | 6.60 (running) |
| client-default | 31.25 / 52.19 (running / running) | 25.59 / 89.17 (running / running) |
| client-fps1 | 0.93 / 0.93 (running / running) | 5.00 / 2.20 (running / running) |
| client-fps144 | 34.69 / 62.81 (running / running) | 39.79 / 122.56 (running / running) |
| server-loss | 0.00 / 0.00 (exit 1 / running) | 0.00 / 0.40 (exit -9 / running) |
| closed-output | 1.25 / 1.56 (running / exit 1) | 0.80 / 0.60 (running / exit 1) |
| mirror-default | 5.62 (running) | 28.79 (running) |
| mirror-fps144 | 29.38 (running) | 67.38 (running) |
| file-pause | 0.94 (running) | 5.20 (running) |
| acds | 0.00 (running) | 0.40 (running) |
| mirror-closed-output | 0.00 (exit 0) | 0.40 (exit 0) |
| mirror-waveform-remote | 0.00 (exit 2) | 0.00 (exit 2) |
| mirror-fft-remote | 0.00 (exit 2) | 0.00 (exit 2) |
| client-waveform-remote | 31.25 / 87.50 (running / running) | 27.79 / 197.52 (running / running) |
| client-fft-remote | 31.56 / 88.44 (running / running) | 27.79 / 197.72 (running / running) |
| client-websocket | 75.62 / 70.94 (running / running) | 32.59 / 92.57 (running / running) |
| client-file-pause | 33.12 / 53.12 (running / running) | 24.39 / 51.98 (running / running) |
| mirror-invalid-camera | 0.00 (exit 2) | 0.00 (exit 2) |
| mirror-strip-ansi | 15.00 (running) | 35.99 (running) |
| mirror-http | 0.00 (exit 0) | 0.00 (exit 0) |
| render-file-png | 0.00 (exit 8) | 0.00 (exit 0) |
| server-status-long | 2.73 (running) | 7.00 (running) |

## Multiple clients and error paths

| Scenario | Windows CPU (state) | Linux CPU (state) |
|---|---|---|
| multi-2 | 63.54 / 72.14 / 41.15 (running / running / running) | 84.50 / 70.66 / 71.16 (running / running / running) |
| multi-4 | 197.92 / 74.74 / 73.96 / 78.12 / 77.34 (running / running / running / running / running) | 214.32 / 94.99 / 72.50 / 72.16 / 71.49 (running / running / running / running / running) |
| acds-status | 0.00 (running) | 0.60 (running) |
| mirror-url-failed | 0.00 (running) | 0.00 (exit 26) |
| mirror-stdin-eof | 0.00 (exit 26) | 0.00 (exit 26) |
| mirror-media-audio | 0.00 (exit 2) | 0.00 (exit 2) |

## Corrected audio visualization variants

| Scenario | Windows CPU (state) | Linux CPU (state) |
|---|---|---|
| mirror-waveform-valid | 40.00 (running) | 98.17 (running) |
| mirror-fft-valid | 42.19 (running) | 122.76 (running) |
| mirror-media-audio-valid | 6.88 (running) | 12.80 (running) |

## Lifecycle, terminal, and discovery probes

| Scenario | Platform / phase | CPU (state) |
|---|---|---|
| silent-handshake | Windows baseline | 0.00 (running) |
| mirror-closed-output | Windows baseline | 103.73 (running) |
| mirror-blocked-output | Windows baseline | 6.54 (running) |
| discovery-waiting-for-peer | Windows baseline | 0.16 / 10.94 (running / running) |
| discovery-service-lost | Windows baseline | 0.00 / 5.21 (exit 1 / exit 40) |
| silent-handshake | Linux baseline | 0.25 (running) |
| mirror-closed-output | Linux baseline | 78.58 (running) |
| mirror-blocked-output | Linux baseline | 23.19 (running) |
| pty-mirror | Linux baseline | 29.66 (running) |
| pty-paused | Linux baseline | 5.50 (running) |
| pty-small-terminal | Linux baseline | 31.83 (running) |
| pty-help | Linux baseline | 26.16 (running) |
| pty-terminal-closed | Linux baseline | 54.66 (running) |
| pty-sigterm | Linux baseline | 0.33 (exit 0) |
| pty-client | Linux baseline | 46.83 / 84.33 (running / running) |
| pty-server-stopped | Linux baseline | 0.00 / 2.33 (running / running) |
| pty-server-killed | Linux baseline | 0.00 / 1.00 (exit -9 / running) |
| pty-client-sigterm | Linux baseline | 0.67 / 0.67 (running / exit 1) |
| discovery-waiting-for-peer | Linux baseline | 0.40 / 29.60 (running / running) |
| discovery-service-lost | Linux baseline | 0.00 / 29.66 (exit -9 / running) |
| pty-paused | Linux fixed | 6.17 (running) |
| pty-small-terminal | Linux fixed | 30.50 (running) |
| pty-help | Linux fixed | 24.33 (running) |
| pty-terminal-closed | Linux fixed | 30.16 (running) |
| pty-sigterm | Linux fixed | 0.33 (exit 0) |
