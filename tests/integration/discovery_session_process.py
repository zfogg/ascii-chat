"""Run discovery state-machine checks with an isolated TOFU/config directory."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import os
from pathlib import Path
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="ascii-discovery-process-") as directory:
    for suffix in ["ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"]:
        (Path(directory) / suffix).mkdir(parents=True, exist_ok=True)
    env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
    env.update(HOME=directory, USERPROFILE=directory, APPDATA=directory, XDG_CONFIG_HOME=directory,
               ASCII_CHAT_QUESTION_PROMPT_RESPONSE="y", ASCII_CHAT_TEST_HOME=directory)
    result = subprocess.run([str(binary), *sys.argv[2:]], env=env, timeout=90)
    if result.returncode:
        print(f"Discovery process fixture exited with {result.returncode}", flush=True)
    raise SystemExit(result.returncode)
