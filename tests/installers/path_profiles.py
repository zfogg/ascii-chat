"""Exercise real Bash/Zsh startup files and Y/n prompts in a disposable PTY.

Run: python3 tests/installers/path_profiles.py web/www/public/install.sh
Uses only the standard library; requires Bash and Zsh on PATH.
"""

import errno
import os
from pathlib import Path
import pty
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


installer = Path(sys.argv[1]).read_text()
# Test the actual profile helper without repeatedly downloading release archives.
helper = installer.split("# Download the latest release.", 1)[0]
bash = os.environ.get("TEST_BASH", "/bin/bash")
assert shutil.which("zsh"), "Install Zsh before running these tests"


def run(home, bindir, replies="\ny\n", extra=None, terminal=True):
    env = dict(os.environ, HOME=str(home), SHELL=bash)
    env.pop("SUDO_USER", None)
    env.pop("ZDOTDIR", None)
    env.update(extra or {})
    code = "set -eu\n" + helper + '\nascii_chat_configure_path "$1"\n'
    command = [bash, "-c", code, "path-test", str(bindir)]
    if not terminal:
        result = subprocess.run(command, env=env, stdin=subprocess.DEVNULL,
                                capture_output=True, text=True, start_new_session=True,
                                timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout + result.stderr
    pid, fd = pty.fork()
    if pid == 0:
        os.execve(bash, command, env)
    output = b""
    answers = iter(replies.splitlines(keepends=True))
    prompts = 0
    deadline = time.monotonic() + 30
    try:
        while time.monotonic() < deadline:
            if select.select([fd], [], [], 0.1)[0]:
                try:
                    data = os.read(fd, 65536)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    break
                if not data:
                    break
                output += data
                if output.count(b"[Y/n]") > prompts:
                    prompts += 1
                    os.write(fd, next(answers, "n\n").encode())
        else:
            os.kill(pid, signal.SIGKILL)
            raise AssertionError("Prompt timed out: " + output.decode())
    finally:
        os.close(fd)
    _, status = os.waitpid(pid, 0)
    assert status == 0, output.decode()
    return output.decode()


with tempfile.TemporaryDirectory(prefix="ascii-chat-path-") as temp:
    root = Path(temp)
    bindir = root / "bin with spaces ' and $dollar [glob]"
    bindir.mkdir()

    def home(name):
        directory = root / name
        directory.mkdir()
        return directory

    accepted = home("accepted")
    out = run(accepted, bindir)
    assert "for bash? [Y/n]" in out and "for zsh? [Y/n]" in out, out
    profiles = [accepted / name for name in (".bashrc", ".bash_profile", ".zshrc")]
    before = {p: p.read_bytes() for p in profiles}
    for profile in profiles:
        for shell in (bash, shutil.which("zsh")):
            # Re-sourcing any generated block must not duplicate PATH entries.
            result = subprocess.run(
                [shell, "-c", '. "$1"; . "$1"; printf "%s" "$PATH"',
                 "check", str(profile)], capture_output=True, text=True, check=True)
            assert result.stdout.split(":").count(str(bindir)) == 1, result.stdout
    out = run(accepted, bindir)
    assert "[Y/n]" not in out, out
    assert before == {p: p.read_bytes() for p in profiles}

    declined = home("declined")
    run(declined, bindir, "n\nn\n")
    assert not list(declined.iterdir())

    present = home("present")
    out = run(present, bindir, extra={"PATH": str(bindir) + ":" + os.environ["PATH"]})
    assert "[Y/n]" not in out and not list(present.iterdir()), out

    for configured_shell in ("bash", "zsh"):
        directory = home("configured-" + configured_shell)
        names = (".bashrc", ".bash_profile") if configured_shell == "bash" else (".zshrc",)
        for name in names:
            (directory / name).write_text("export PATH=" + shlex.quote(str(bindir)) + ':"$PATH"\n')
        originals = {p: p.read_bytes() for p in directory.iterdir()}
        out = run(directory, bindir, "n\nn\n")
        assert "for " + configured_shell + "? [Y/n]" not in out, out
        assert originals == {p: p.read_bytes() for p in directory.iterdir()}

    login = home("existing-login")
    (login / ".bash_login").write_text("# preserve existing login file\n")
    run(login, bindir)
    assert not (login / ".bash_profile").exists()
    assert (login / ".bash_login").read_text().startswith("# preserve existing login file\n")

    zdot_home = home("zdot-home")
    zdot = home("zdot-config")
    run(zdot_home, bindir, extra={"ZDOTDIR": str(zdot)})
    assert (zdot / ".zshrc").exists() and not (zdot_home / ".zshrc").exists()

    headless = home("headless")
    out = run(headless, bindir, terminal=False)
    assert "[Y/n]" not in out and "export PATH=" in out
    assert not list(headless.iterdir())
    elevated = home("sudo")
    out = run(elevated, bindir, extra={"SUDO_USER": "test-user"})
    assert "[Y/n]" not in out and not list(elevated.iterdir())

print("PASS: Bash/Zsh Y/n defaults, decline, startup PATH detection, idempotence, "
      "quoting, login profile selection, ZDOTDIR, headless and sudo behavior")
