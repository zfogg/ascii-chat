"""Run discovery state-machine checks with an isolated TOFU/config directory."""
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
    result = subprocess.run([str(binary)], env=env, timeout=45)
    if result.returncode:
        print(f"Discovery process fixture exited with {result.returncode}", flush=True)
    raise SystemExit(result.returncode)
