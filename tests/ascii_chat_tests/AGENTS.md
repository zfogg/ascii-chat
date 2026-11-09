# ascii-chat Python test environment

This directory is the `uv` project for ascii-chat's Python test harnesses and
native probes. It owns the Python test dependencies and `uv.lock`.

Use the repository-pinned Python through `uv`:

```bash
uv sync --project tests/ascii_chat_tests --frozen
uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/unit
```

CTest invokes Python tests through this environment. When configuring CMake,
point `Python3_EXECUTABLE` at `tests/ascii_chat_tests/.venv/bin/python` (or the
platform-equivalent path). Keep Python dependencies in this project's
`pyproject.toml`, regenerate `uv.lock` with `uv lock`, and do not install them
with system `pip`.

Native probes use `ctypes` against built ascii-chat libraries. Keep their C
signatures synchronized with the exported headers and test both success and
failure paths on supported platforms.
