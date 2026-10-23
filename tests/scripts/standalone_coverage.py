"""Shared coverage output handling for dependency-isolated standalone tests."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401


from contextlib import contextmanager
import os
from pathlib import Path
import tempfile


@contextmanager
def standalone_output_directory(name):
    """Yield a directory that retains profiles in a native coverage build."""
    if os.environ.get("ASCIICHAT_COVERAGE_BUILD") == "1":
        root = Path(os.environ["ASCIICHAT_COVERAGE_ROOT"]).resolve()
        output = root / "standalone-coverage" / name
        output.mkdir(parents=True, exist_ok=True)
        yield output
        return
    with tempfile.TemporaryDirectory(prefix=f"ascii-chat-{name}-") as directory:
        yield Path(directory)


def coverage_flags():
    """Return Clang/GCC-compatible instrumentation flags for standalone builds."""
    if os.environ.get("ASCIICHAT_COVERAGE_BUILD") == "1":
        return ["--coverage"]
    return []
