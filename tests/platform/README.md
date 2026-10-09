# Process title smoke tests

Native mode dispatch sets `ascii-chat: <canonical-mode> mode`. Linux and macOS
update the command line visible in `ps -ww -p PID -o args=`; Windows updates the
console title. This does not rename the executable, Linux `comm`, or the macOS
Activity Monitor process name. Browser/WASM builds do nothing.

Unix initialization preserves argument and environment strings before option
parsing. Shutdown restores their original storage. Titles that cannot fit fail
without truncation; title failures do not prevent a mode from starting. Windows
console failures never fall back to writing title escapes into stdout.

Run the Unix backend tests with Python 3 and Clang:

```sh
python3 tests/platform/process_title_smoke.py
CFLAGS='-fsanitize=address,undefined' python3 tests/platform/process_title_smoke.py
CC=musl-clang CFLAGS=-static python3 tests/platform/process_title_smoke.py
```

These compile the production backend using lightweight allocation/error shims,
then inspect each child's actual `ps` command line for all five title strings.
They also check preserved argv/environment values, setenv/unsetenv after
initialization, invalid/oversized titles, repeated updates, and cleanup. They
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
