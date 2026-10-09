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
python tests/ui/test_invitation.py build/bin/asciichat.dll
```

On Linux/macOS, pass the built `libasciichat.so` or `libasciichat.dylib` instead.
These tests do not require Criterion.

The Windows integration test uses ConPTY and a local discovery service. Install
`pywinpty` and `pyte` in a test virtual environment, then run:

```powershell
python tests/ui/test_discovery_terminal.py build/bin/ascii-chat.exe
```

It verifies the rendered waiting screen with a local camera test pattern,
shrinking/growing the terminal, a TCP peer sending capabilities without camera
frames, a real two-peer WebRTC discovery connection, Ctrl+C, disabled splash,
piped snapshot output, and connection failure cleanup. The TCP capabilities test
exercises the host transport directly; it does not test ACDS TCP host negotiation.

The test chooses ephemeral local ports, creates its database under the build
directory, and cleans up the processes it starts. Terminal transcripts, screen
text, and logs are saved in `build/invitation-terminal-test/`. The ordinary
application discovery identity/configuration behavior still applies.

Splash lifecycle regressions can also be checked against the shared library:

```powershell
python tests/ui/test_splash_lifecycle.py build/bin/asciichat.dll
```

This checks that a zero-delay snapshot skips the minimum splash duration and
that shutdown interrupts a handoff waiting on a full output pipe. Each case
runs in an isolated subprocess with a timeout. These checks have been verified
on Windows.

On Linux with tmux installed, run the native call test (also suitable for Docker):

```sh
python tests/ui/test_discovery_tmux.py build/bin/ascii-chat build/tmux-validation
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
