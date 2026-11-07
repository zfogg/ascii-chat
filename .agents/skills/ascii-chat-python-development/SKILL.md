---
name: ascii-chat-python-development
description: Develop or maintain Python code and tooling in the ascii-chat repository, including uv dependencies, linting, and type checking.
---

# ascii-chat Python development

Work from the repository root. Read the applicable `AGENTS.md` and inspect the nearest `pyproject.toml` and `uv.lock`: this repository has separate Python projects, including `tests/ascii_chat_tests` and `src/tooling/defer`. Use the project that owns the code rather than assuming one environment covers the repository.

Manage dependencies with `uv`; update the owning project's lockfile when dependencies change. Use `uv sync --project <project> --frozen` to reproduce a locked environment and `uv run --project <project> --frozen ...` to run its tools. Respect the project's Python version and platform constraints. Avoid committing virtual environments or generated caches.

Run Ruff and ty for the Python code affected by a change, using the owning project's configuration and environment. Resolve their errors before committing. For the defer tool, from the repository root:

```sh
uv run --project src/tooling/defer --frozen ruff check src/tooling/defer
uv run --project src/tooling/defer --frozen ty check --project src/tooling/defer src/tooling/defer
```

Consult the [ascii-chat testing skill](../ascii-chat-testing/SKILL.md) for Python test placement, execution, and the test tree's Ruff and ty checks. Run relevant behavioral tests as well as static checks; report platform limitations and any checks that could not run.

Keep scripts import-safe when they are imported by tests or other modules. For subprocesses and paths, account for Windows, Linux, and macOS when the tool is cross-platform; pass argument lists rather than relying on shell quoting. Preserve existing files and caches unless the task requires changing them.
