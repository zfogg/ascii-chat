# Process title smoke tests

Native mode dispatch sets `ascii-chat: <canonical-mode> mode - <arguments>`. The
executable and explicit mode token are omitted from the argument list; implicit
discovery keeps its session string. With no arguments, the separator is omitted.
The same platform API updates all supported naming surfaces:

- Linux: full command line in `ps -ww -p PID -o args=`, plus `/proc/PID/comm`
  and `ps -o comm=`. The 15-byte comm limit uses distinct names: `ascii:server`,
  `ascii:client`, `ascii:mirror`, `ascii:discovery`, and `ascii:acds`.
- macOS: full command line, main-thread name, and the full sanitized Launch
  Services display name used by Activity Monitor. Dynamic private API lookup
  lets naming fail gracefully when Launch Services is unavailable. Registration
  uses `LSUIElement` so a terminal process does not acquire a Dock icon.
- Windows: console title via the terminal wrapper.
- Browser/WASM: no-op.

This does not rename the executable file or Windows Task Manager image name.
Raw `platform_process_title_set()` calls update all naming surfaces too, using
the supplied title as the short name (subject to OS length limits). The
mode-aware call chooses the compact names above. Both setters are main-thread
only. If the extra native naming operation fails, the updated Unix command line
remains in place and the API returns an error for the caller to handle.

Unix initialization preserves argument and environment strings before option
parsing. Shutdown restores their original storage. The argument formatter
truncates sanitized output to fit available storage (or the Windows title limit),
never falling back to the raw command line. Title failures do not prevent a mode
from starting. Windows
console failures never fall back to writing title escapes into stdout.

The formatter replaces values of `--password`, `--websocket-tls-key-password`,
`--turn-credential`, and `--turn-secret` with `[redacted]`, supporting both separate
and `--option=value` forms. It retains key file paths and GPG references: the native
CLI does not accept inline private-key material. Armored private-key text is
nevertheless redacted defensively. Whitespace/empty arguments are quoted and
control characters are escaped. Configuration and environment values are never
added to the title. This is display redaction, not protection of OS launch records
or a guarantee that secrets were never visible before startup completed.

Run the Unix backend tests with Python 3 and Clang:

```sh
python3 tests/platform/process_title_smoke.py
CFLAGS='-fsanitize=address,undefined' python3 tests/platform/process_title_smoke.py
CC=musl-clang CFLAGS=-static python3 tests/platform/process_title_smoke.py
```

These compile the production backend using lightweight allocation/error shims,
then inspect each child's actual `ps` command line for all five title strings.
Linux also checks `/proc/PID/comm` and `ps -o comm=`. On macOS an independent
AppKit probe reads `NSRunningApplication.localizedName` and checks that the
activation policy is not a regular Dock application.
They also check secret redaction, quoting, preserved argv/environment values,
setenv/unsetenv after initialization, invalid/oversized titles, repeated updates,
and cleanup. They
do not substitute for building the application against its real headers.

With Emscripten and Node available, compile and run the WASM no-op path:

```sh
CC=emcc CFLAGS='-sEXIT_RUNTIME=1 -sENVIRONMENT=node' \
  python3 tests/platform/process_title_smoke.py --wasm
```

After a native Windows build:

```powershell
python tests/platform/process_title_windows.py build/bin/asciichat.dll
```

This checks the built DLL's title wrapper and error paths in a hidden console,
then launches the built executable in all five modes and reads each console
title back. Network clients use loopback, servers use temporary ports, discovery
databases/logs are temporary, and media uses a test pattern with audio disabled.
It also verifies a piped mirror snapshot exits successfully without OSC title
output. The current CLI accepts `discovery-service`; it rejects `acds` before
mode dispatch.
