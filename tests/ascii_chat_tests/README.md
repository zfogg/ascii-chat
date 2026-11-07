# ascii-chat-tests

Reusable Python fixtures for the native integration and smoke tests. Scripts under
`tests/` add that directory to `sys.path` before loading `_environment`, so they
can import the package directly. For use outside the repository, install it with
`uv sync --project tests/ascii_chat_tests` and run scripts with
`uv run --project tests/ascii_chat_tests python <script>`.
For CMake, point `Python3_EXECUTABLE` at that project’s `.venv` interpreter.

- `ascii_chat_tests.process`: `Service`, `running`, `interrupt`, `free_port`,
  `server_options`, and `check_client` for loopback processes and CLI fixtures.
- `ascii_chat_tests.terminal`: `Terminal`, `TerminalProcess`, `CaptureScreen`, `CaptureStream`,
  `TitleTerminal`, `capture`, and `load_terminal_font` for PTY/ConPTY interaction and screen capture.
- `ascii_chat_tests.tmux`: `Pane` and `tmux` for tmux-driven scenarios.

The standard environment includes PTY and screen-capture dependencies.
The process fixtures expect an explicit built binary path. `free_port()` releases
the port immediately, so callers must still handle the normal bind race.

Additional helpers:

- `ascii_chat_tests.cli`: `cli_command` builds a native invocation with update
  checks disabled; `run_cli` and `start_cli` pass subprocess options through;
  `clean_environment` removes inherited ascii-chat options.
- `ascii_chat_tests.media`: `lavfi_fixture` builds synthetic FFmpeg media with
  explicit sources and output options. It requires FFmpeg on PATH unless an
  executable is supplied. `ppm_image` and `write_pcm_wav` create simple image
  and PCM fixtures without FFmpeg; `reconstruct` decodes Kitty graphics.
- `ascii_chat_tests.protocol`: `crc32c` supports ACIP packet fixtures.
- `ascii_chat_tests.timing`: receive-time parsing and interval checks.
- `ascii_chat_tests.jsonrpc`: `JsonRpcProcess` runs JSON-lines subprocesses with
  bounded reads, notification handling and cleanup.
- `ascii_chat_tests.audio_spectral`, `processor_ctypes`, and `pipeline_ctypes`
  provide native audio bindings. Install the `audio` extra with `uv sync
  --project tests/ascii_chat_tests --extra audio`.

`clean_environment` removes all `ASCII_CHAT_` settings, including test policy
settings. Tests that need particular settings must add them back explicitly.

## Running Python tests

The locked project includes pytest and pytest-xdist. Migrated tests are in
`tests/python/`; default collection is restricted to that directory because
legacy harnesses elsewhere may execute at import time. From the repository root:

```sh
uv sync --project tests/ascii_chat_tests --frozen
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/unit
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python --ascii-chat-binary build/bin/ascii-chat --terminal-probe build/bin/test-terminal-pty --lifecycle-probe build/bin/test-client-like-lifecycle --media-capture-probe build/bin/test-media-capture-open
ctest --test-dir build -L '^python$' --no-tests=error --output-on-failure --parallel
```

On Windows use `build/bin/ascii-chat.exe`. Pytest defaults to all logical cores
with `-n logical`; use `-n 0` to debug a case. CTest reserves all cores for each
pytest batch to avoid nested oversubscription. It retains its existing resource
serialization for legacy scenarios. Loopback network tests share a lock because each server creates a large worker pool. Pytest case reports are written to
`build/Testing/python-*.xml`, alongside the CTest report. CTest uses a separate
build-local uv environment so Docker and native builds do not replace each
other's virtual environments. Install `uv` before configuring with tests enabled.
