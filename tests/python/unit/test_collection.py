"""Collection must not execute native processes or alter the test environment."""
from pathlib import Path
import os
import runpy
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
MODULES = sorted(path for path in ROOT.rglob("test_*.py") if path != Path(__file__).resolve())


@pytest.mark.parametrize("module", MODULES, ids=lambda path: str(path.relative_to(ROOT)))
def test_module_import_is_side_effect_free(module, monkeypatch):
    def unexpected(*args, **kwargs):
        pytest.fail(f"{module} started a subprocess during import")
    monkeypatch.setattr(subprocess.Popen, "__init__", unexpected)
    monkeypatch.setattr(subprocess, "run", unexpected)
    before = dict(os.environ)
    runpy.run_path(str(module), run_name="pytest_import_safety_check")
    assert dict(os.environ) == before
