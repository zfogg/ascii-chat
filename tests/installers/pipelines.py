"""Verify the website's exact Bash commands preserve download/installer errors.

Usage: python3 tests/installers/pipelines.py web/www/src/components/home/InstallationSection.tsx
Download and sudo functions are stubbed; both pipeline shells are real.
"""
from pathlib import Path
import re
import shutil
import subprocess
import sys

commands = re.findall(r'command="([^"\n]*curl[^"\n]*)"', Path(sys.argv[1]).read_text())
assert len(commands) == 2, "Expected both user and system-wide Bash commands"
for shell in ("bash", "zsh"):
    assert shutil.which(shell), "Install " + shell + " before running this test"
    for command in commands:
        for download_exit, installer_exit, expected in ((22, 0, 22), (0, 17, 17), (0, 0, 0)):
            setup = (
                f"curl() {{ printf 'exit {installer_exit}\\n'; return {download_exit}; }}\n"
                'sudo() { "$@"; }\n'
            )
            result = subprocess.run([shell, "-c", setup + command], capture_output=True, text=True)
            assert result.returncode == expected, (shell, command, expected, result.returncode, result.stderr)
print("PASS: both website commands propagate download and installer failures in Bash and Zsh")
