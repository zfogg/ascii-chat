## Instrumented Builds

- **Enable** by configuring CMake with `-DASCII_BUILD_WITH_INSTRUMENTATION=ON` (available on every preset). The flag builds the `ascii-instr-tool`, generates an instrumented source tree inside `build/instrumented/`, and rewrites targets to compile those files instead of the originals.
- **Artifacts**: every instrumented translation unit emits per-statement calls to `ascii_instr_log_line`. At link time the executable (and any exported libraries) pull in `ascii-debug-runtime`, so no additional manual linking is required.
- **Regeneration** happens automatically: touching a source or rebuilding `ascii-instr-tool` invalidates `instrumented/.instrumented.stamp`, forcing the tool to rerun before any module compiles. You can inspect the transformed sources or tail the per-thread logs directly from that directory.

### Runtime Filters

- `ASCII_INSTR_INCLUDE`/`ASCII_INSTR_EXCLUDE`: substring filters applied to the source path recorded by the tool.
- `ASCII_INSTR_THREAD`: restrict output to specific thread IDs (multiple IDs can be comma-separated).
- `ASCII_INSTR_OUTPUT_DIR`: override the directory the runtime uses for per-thread log files. Default falls back to `TMPDIR`, `TEMP`, or `/tmp`.

The runtime writes one file per PID/TID and rate-limits I/O through `platform_write`, so it remains safe in high-frequency code paths.

### Manual Invocations

- The helper script `cmake/debug/run_instrumentation.sh` remains available when you need to instrument a custom subset of files (e.g., targeting an experimental branch outside the CMake flow). Export `ASCII_INSTR_TOOL` to point at a different binary if required.
- You can also call `ascii-instr-tool` directly: `build/bin/ascii-instr-tool -p build --output-dir /tmp/instrumented --input-root $(pwd) --file-list sources.txt` (the CMake integration auto-generates `sources.txt`).

### Caveats

- Third-party code under `deps/` and the instrumentation runtime itself are deliberately excluded to avoid recursive logging.
- Instrumented builds still honor sanitizers and presets; however, expect significantly more logging output, so consider redirecting with `--log-file` during interactive tests.

