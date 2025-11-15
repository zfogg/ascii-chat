## Source Print Instrumented Builds

### When to Enable

- Configure any preset with `-DASCII_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON` to enable the source_print instrumentation pipeline. Examples:
  - `cmake --preset default -B build -DASCII_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION=ON`
  - `cmake --build build`
- The configure step adds the source_print instrumentation tool (`ascii-instr-source-print`, a Clang libTooling rewriter) and the `ascii-debug-runtime` library to the build graph. When the flag is on, targets listed in `cmake/tooling/Instrumentation.cmake` are rebuilt from a source_print-instrumented tree under `build/instrumented/`.
- The generated tree mirrors project layout but skips third-party code (`deps/`) and the source_print instrumentation runtime itself to avoid recursive logging.

### Build Flow

1. The source_print instrumentation tool (`ascii-instr-source-print`) rewrites each translation unit, inserting `ascii_instr_log_line()` before every executable statement.
2. `cmake/tooling/run_instrumentation.sh` orchestrates the source_print pass, refuses to reuse populated directories, and writes outputs only to `build/instrumented/`.
3. A stamp file (`build/instrumented/.stamp`) records successful completion. Modifying sources or the tool invalidates the stamp and triggers regeneration.
4. Library/executable targets acquire an explicit dependency on the generation step and link against `ascii-debug-runtime` when required.

### Generated Layout

- `build/instrumented/<relative/path>.c` — source_print-instrumented copy of each source file.
- `build/instrumented/.stamp` — sentinel touched after a successful run.
- Only files that required source_print instrumentation appear; directories are created lazily alongside rewritten files.

### Source Print Macro Instrumentation Controls

- Pass `--log-macro-expansions` to record statements that originate from macro bodies (disabled by default to limit noise).
- Pass `--log-macro-invocations` to emit a synthetic record for each macro call site (deduplicated per expansion). The log entry is tagged with `macro=2` and contains the invocation snippet.
- The legacy flag `--include-macro-expansions` remains accepted as an alias for `--log-macro-expansions` but prints a warning. Update any scripts to the new flag names.

Inspect source_print-instrumented files whenever you need to confirm macro expansion handling or verify that inserted snippets match expectations.

## Source Print Runtime Logging

### Per-thread Log Files

- Each thread lazily allocates a runtime state block the first time it logs. Logs live in `${ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR:-$TMPDIR:-$TEMP:/tmp}` and use the pattern `ascii-instr-<pid>-<tid>.log`.
- Files open with `O_CREAT|O_EXCL`, so stale logs are never appended to. Rename or archive log files between runs if you want to keep history.
- If file creation fails, the runtime flips into `stderr` mode while keeping the same record format.

### Log Record Format

```
pid=12345 tid=678 seq=42 ts=2025-11-07T18:32:01.123456789Z elapsed=12.3ms file=lib/network.c line=512 func=socket_send macro=0 snippet=send_packet(queue, payload)
```

- `elapsed` displays monotonic time since the runtime initialized inside the process.
- `macro` values distinguish statement origin:
  - `0` — standard source statements.
  - `1` — statements emitted from macro expansions.
  - `2` — synthetic macro invocation records (enabled via `--log-macro-invocations`).
- `snippet` escapes control characters (`\n`, `\t`, `\r`) and truncates to 2048 characters to stay within the 4 KiB atomic write budget.

### Environment Filters

| Variable | Purpose |
| --- | --- |
| `ASCII_INSTR_SOURCE_PRINT_INCLUDE` | Substring filter applied to `file=`; emit only when the substring is present. |
| `ASCII_INSTR_SOURCE_PRINT_EXCLUDE` | Suppresses logs when the substring matches the file path. |
| `ASCII_INSTR_SOURCE_PRINT_THREAD` | Comma-separated decimal thread IDs to keep. IDs must match those printed in log headers. |
| `ASCII_INSTR_SOURCE_PRINT_INCLUDE_REGEX` | POSIX extended-regular-expression include filter on `file=`. Takes precedence over substring filters. |
| `ASCII_INSTR_SOURCE_PRINT_EXCLUDE_REGEX` | POSIX extended-regular-expression exclude filter on `file=`. |
| `ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE` | Substring filter on `func=`. |
| `ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE` | Substring exclusion filter on `func=`. |
| `ASCII_INSTR_SOURCE_PRINT_FUNCTION_INCLUDE_REGEX` | POSIX extended regex include filter for function names. |
| `ASCII_INSTR_SOURCE_PRINT_FUNCTION_EXCLUDE_REGEX` | POSIX extended regex exclusion filter for function names. |
| `ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR` | Override log directory. The runtime creates it with mode 0700 when absent. |
| `ASCII_INSTR_SOURCE_PRINT_ONLY` | Comma-separated allow-list selectors (`file=<glob>`, `func=<glob>`, `module=<name>[:<glob>]`, or shorthand `module:pattern` such as `server:*`). |
| `ASCII_INSTR_SOURCE_PRINT_RATE` | Positive integer; log the first statement and every Nth subsequent statement per thread (helps throttle noisy traces). |
| `ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE` | Set to `1`, `true`, `on`, or `yes` to enable SanitizerCoverage logging callbacks (requires building with `-fsanitize-coverage=trace-pc-guard`). |

> Regex filters rely on `regcomp(3)` and are available on POSIX platforms. On Windows they are ignored gracefully.

Unset variables disable the corresponding filter. Checks happen before log formatting to reduce overhead.

`ASCII_INSTR_SOURCE_PRINT_ONLY` is useful when you want to lock tracing to a narrow slice of the project:

- `ASCII_INSTR_SOURCE_PRINT_ONLY=file=lib/network/*,func=enforce_*` keeps only logs whose file path matches the glob and whose function names match `enforce_*`.
- `ASCII_INSTR_SOURCE_PRINT_ONLY=module=server:render_*` restricts logging to files under any `/server/` directory whose basename matches `render_*`.
- `ASCII_INSTR_SOURCE_PRINT_ONLY=server:*` is shorthand for “any file living under a `/server/` directory”.

### Startup & Shutdown

- `ascii_instr_runtime_get()` uses `pthread_once` to create TLS keys and picks up environment filters. Memory is allocated through the SAFE_ macros, ensuring leak tracking when `DEBUG_MEMORY` is enabled.
- `ascii_instr_runtime_global_shutdown()` tears down TLS entries and halts logging—useful in unit tests that spawn threads.

### Sanitizer Coverage Mode

- Build with `-fsanitize-coverage=trace-pc-guard` (or enable the equivalent CMake option) and export `ASCII_INSTR_SOURCE_PRINT_ENABLE_COVERAGE=1`.
- The runtime registers `__sanitizer_cov_trace_pc_guard` and logs a compact `pc=0x...` snippet for each sampled edge, sharing the same per-thread files as statement logs.
- Combine with `llvm-symbolizer` or `addr2line` in post-processing to map PCs back to `file:line`. Coverage entries respect `ASCII_INSTR_SOURCE_PRINT_RATE` so you can throttle high-volume traces.

## Usage Workflows

### CMake-managed builds

1. Configure with source_print instrumentation enabled (see above).
2. Build normally (`cmake --build build`). Compilers will consume the source_print-instrumented sources transparently.
3. Export runtime filters as needed, e.g.:
   - `export ASCII_INSTR_SOURCE_PRINT_INCLUDE=network.c`
   - `export ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR=/tmp/ascii-instr`
4. Run the binary and inspect `/tmp/ascii-instr/ascii-instr-<pid>-<tid>.log`. `tail -n1` reveals the last executed statement per thread.

### Manual source_print instrumentation

Use the source_print instrumentation helper script when experimenting with subsets of files or out-of-tree builds:

```
./cmake/tooling/run_instrumentation.sh -b build -o /tmp/ascii-instrumented -- lib/audio/audio.c src/server/server.c
```

- Additional clang-tool flags can follow the `--` delimiter (e.g., `--filter-file server`).
- The script reuses the repository root as `--input-root`, keeping relative paths stable.

### Thread-focused inspection

When diagnosing a multi-threaded crash, identify the failing thread ID from the crash report or sanitizer output, then:

```
grep "tid=1234" /tmp/ascii-instr-*.log | tail -n1
```

The final `seq=` value indicates the last statement that thread executed before the fault.

### Summaries with `ascii-instr-report`

- Build the helper with `cmake --build build --target ascii-instr-report`.
- By default the tool scans `${ASCII_INSTR_SOURCE_PRINT_OUTPUT_DIR}` (or falls back to `/tmp`) and prints the most recent record per thread.
- Common flags:
  - `--log-dir /path/to/logs` – override the directory
  - `--thread 1234` – focus on one or more thread IDs (repeat flag to add more)
  - `--include network.c` / `--exclude image2ascii` – apply the same substring filters as the runtime
  - `--raw` – emit the original log line instead of the formatted summary
- Example:

```
./build/bin/ascii-instr-report --log-dir /tmp/ascii-instr --thread 6789
```

The formatted output highlights timestamp, file/line, function, macro flag, and snippet for each thread’s last recorded statement.

## Signal Handler Annotations

- Mark functions that must remain async-signal-safe with `ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER` before the declaration:

```c
ASCII_INSTR_SOURCE_PRINT_SIGNAL_HANDLER
void handle_sigint(int signum) {
    /* ... */
}
```

- The source_print instrumentation tool skips any function carrying that annotation, ensuring inserted logging calls never appear inside signal handlers.
- Combine this with the runtime filters to keep critical paths free from additional I/O.

## Safety Guarantees

- **Original sources remain untouched.** The tool writes only to new files under `build/instrumented/` and aborts if the destination already exists.
- **Atomic writes.** Each log line is emitted with a single `platform_write()` call, so concurrent threads cannot interleave records, and logs survive abrupt crashes.
- **Signal tolerance.** Once initialized, the runtime avoids non signal-safe functions inside the hot path, making it reliable even when a crash occurs between statements.
- **Thread isolation.** Separate files per thread prevent interleaving and simplify postmortem analysis.

## Limitations & Best Practices

- **Performance cost.** Logging every statement carries significant overhead. Narrow the source_print instrumentation scope using `ASCII_INSTR_SOURCE_PRINT_INCLUDE` or clang-tool filters when chasing specific bugs.
- **Timing changes.** Heavy I/O can mask race conditions (Heisenbugs). After localizing the failure, confirm with sanitizers (`-fsanitize=address,undefined`) or record/replay (`rr`).
- **Macros and generated code.** Macro bodies receive logs, but the failing line may still reside in called functions. Combine logs with traditional debugging techniques for accuracy.
- **Binary size and build time.** Expect larger binaries and longer links because every statement introduces a logging call and associated string literal.
- **Thread IDs differ by platform.** Always copy the numeric ID from log headers when using `ASCII_INSTR_SOURCE_PRINT_THREAD`; pthread IDs and system thread IDs are not interchangeable.

## Troubleshooting

- **No logs emitted:** verify the output directory exists and is writable, and double-check that filters are not over-restrictive. Clearing `ASCII_INSTR_SOURCE_PRINT_INCLUDE` entirely disables the include filter.
- **`Refusing to overwrite` errors during source_print instrumentation:** remove the existing `build/instrumented/` directory or specify a fresh output directory.
- **Partial log files:** ensure disks are not full. The runtime permanently disables logging if `platform_write()` fails with a non-recoverable error.
- **Missing include warnings:** custom extensions may be skipped by `run_instrumentation.sh`. Update the script’s `find` expression if you have non-standard source suffixes.

## Roadmap Follow-ups

- Cover runtime filtering and formatting with Criterion tests executed via `./tests/scripts/run_tests.sh` (Docker on macOS).

