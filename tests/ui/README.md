# Discovery invitation checks

Build with the usual Clang/Ninja preset:

```sh
cmake --preset default -B build
cmake --build build --target ascii-chat
```

The layout tests call the production renderer through the shared library. They
cover the adjacent session/Run lines, centering, compact terminals, long-string
wrapping, creating/joining states, animation, and terminal bounds:

```sh
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/native/test_ui_probes.py --shared-library build/bin/asciichat.dll -k invitation
```

On Linux/macOS, pass the built `libasciichat.so` or `libasciichat.dylib` instead.
These tests do not require Criterion.

The Windows integration test uses ConPTY and a local discovery service. Install
`pywinpty` and `pyte` in a test virtual environment, then run:

```powershell
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/ui_discovery --ascii-chat-binary build/bin/ascii-chat.exe --run-manual-discovery -k terminal
```

It verifies the rendered waiting screen with a local camera test pattern,
shrinking/growing the terminal, a TCP peer sending capabilities without camera
frames, a real two-peer WebRTC discovery connection, Ctrl+C, disabled splash,
piped snapshot output, and connection failure cleanup. The TCP capabilities test
exercises the host transport directly; it does not test ACDS TCP host negotiation.

The test chooses ephemeral local ports, creates its database under the build
directory, and cleans up the processes it starts. Terminal transcripts, screen
text, and logs are saved in the pytest temporary directory. The ordinary
application discovery identity/configuration behavior still applies.

Splash lifecycle regressions can also be checked against the shared library:

```powershell
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/native/test_ui_probes.py --shared-library build/bin/asciichat.dll -k splash
```

This checks that a zero-delay snapshot skips the minimum splash duration and
that shutdown interrupts a handoff waiting on a full output pipe. Cleanup is also
requested while a renderer still holds its frame buffer, verifying deferred
destruction and subsequent reuse. Each case runs in an isolated subprocess with
a timeout. These checks pass on Windows and Linux with AddressSanitizer.

On Linux with tmux installed, run the native call test (also suitable for Docker):

```sh
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/ui_discovery --ascii-chat-binary build/bin/ascii-chat --run-manual-discovery -k tmux
```

It runs a local ACDS and two default-mode discovery peers in private 80x24 tmux
terminals. No transport override is used. It checks the adjacent Run line,
50x12 resize and restore, disappearance of the invitation on both peers, and
changing two-participant call grids for 15 seconds. Audio is disabled; both
cameras use generated video. Plain and ANSI-colored captures are saved alongside
logs in the output directory. Each test owns and cleans up its tmux server.

Colored grid composition regressions are covered by the focused Criterion suite:

```sh
cmake --build build --target test_unit_video_ascii
build/bin/test_unit_video_ascii --filter '*ascii_create_grid*'
```
