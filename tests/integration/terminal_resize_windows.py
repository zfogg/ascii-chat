"""Run the console resize fixture in a hidden, isolated Windows console."""
import subprocess
import sys

startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
result = subprocess.run(
    [sys.argv[1]], startupinfo=startup, creationflags=subprocess.CREATE_NEW_CONSOLE,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30,
)
sys.stdout.buffer.write(result.stdout)
sys.exit(result.returncode)
