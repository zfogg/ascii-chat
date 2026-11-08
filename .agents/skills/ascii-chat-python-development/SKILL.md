---
name: ascii-chat-python-development
description: Develop and test ascii-chat's Python tooling, test harnesses, native ctypes probes, and build-time Python helpers.
metadata:
  short-description: ascii-chat Python development and testing
---

# ascii-chat Python development

Use the repository's `uv` projects and test harnesses. Python is used for
pytest/CTest integration tests, native `ctypes` probes, standalone utilities,
coverage/report generation, CMake helpers, and code-generation/build helpers.

## Python runtime and dependencies

- Use the latest repository-pinned Python minor version through `uv`; do not
  create a separate virtualenv or install project packages with `pip`.
- Run test-suite Python from the test project:
  `uv run --project tests/ascii_chat_tests --frozen python ...`.
- Synchronize it with:
  `uv sync --project tests/ascii_chat_tests --frozen`.
- Keep dependency declarations in `tests/ascii_chat_tests/pyproject.toml` and
  its lockfile synchronized. Update the lockfile with `uv lock`.
- CMake must receive the interpreter from the `uv` environment when configuring
  Python-backed tests (for example, `-DPython3_EXECUTABLE=.../.venv/bin/python`).
  CTest launches the generated Python tests through `uv`; do not bypass that
  environment with a system `python`.

## Test and probe workflow

- Prefer the narrowest relevant pytest command, then the corresponding CTest
  label or test when the change crosses the CMake integration boundary.
- Native probes load the built shared library with `ctypes`; verify exported
  signatures, ownership, error paths, and platform library loading.
- Keep terminal/network/media tests deterministic and use their existing
  fixtures and command-line options rather than adding ad-hoc sleeps.
- For C/Python boundary changes, inspect both the Python call site and the
  exported C API/header, then run the focused probe and test suite.

## Style and tooling

- Follow the repository's Python formatting and lint configuration. Use `uv run`
  for `pytest`, `ruff`, and `ty`.
- Python scripts should use structured exceptions and actionable failures; do
  not hide subprocess failures or silently accept malformed native data.
- Keep reusable test fixtures in `tests/ascii_chat_tests`; keep repository
  utilities and build helpers in their existing `scripts/`, `cmake/`, or
  `.github/scripts/` locations.
