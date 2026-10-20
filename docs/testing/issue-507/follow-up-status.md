# Discovery and dependency follow-up handoff

PR #531 is merged. The follow-up work from #532 is being transferred directly
to master at the user's request, including the previously uncommitted changes.

The follow-up fixes discovery frame pacing, honors `--no-webrtc` when the host
discloses a TCP endpoint, uses the session clock for pong deadlines, and allows
the fourth discovery negotiation packet required by clients with an identity.
Dependency changes address BearSSL command quoting, Opus header discovery,
discovery-test mimalloc linking, stale musl configuration, and macOS release
dependency paths/static linking.

The direct-TCP integration fixture passed on Windows against the current Debug
binary: password authentication, changing video reaching the host, encrypted
video reaching the client, actual TCP admission, and graceful process shutdown.
This fixture verifies media delivery; completed terminal presentation FPS is
measured separately by `network_fps_windows.py`.

Validation remains incomplete. The final identity-enabled 60-second FPS reruns,
fresh WASM artifacts, and final macOS/Windows dependency CI verification have
not all completed. The macOS source-root literal identified by the hardening
check still needs repair. The working manifest is preserved as work in progress;
it does not certify that artifacts match all final source changes.

Historical transport labels were corrected in `review-follow-up/README.md`.
Intermediate FPS results and failed attempts must not be treated as final proof.
Local build output, scripts, and logs are retained in the main checkout under
`build/issue-507-worktree-archive/` when the old worktree is removed.
