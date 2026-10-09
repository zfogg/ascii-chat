#!/usr/bin/env python3
"""Exercise generated zsh completions in a real tmux terminal.

Usage: python3 tests/integration/zsh_completions.py /path/to/generated.zsh
Requires zsh and tmux. Device enumeration uses deterministic CLI output fixtures.
"""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

script = Path(sys.argv[1]).resolve()
socket = f"ascii-completion-{os.getpid()}"


def tmux(*args):
    return subprocess.check_output(["tmux", "-L", socket, *args], text=True)


with tempfile.TemporaryDirectory(prefix="ascii-completion-") as directory:
    root = Path(directory)
    (root / "ascii-chat").write_text("""#!/bin/sh
case "$1" in
  --list-webcams) printf 'Available webcams:\\n  0 Integrated Camera\\n  2 USB Camera: HD\\n' ;;
  --list-microphones) printf 'Available microphones:\\n  1 USB Microphone (default)\\n  4 Desk Microphone\\n' ;;
  --list-speakers) printf 'Available speakers:\\n  3 Built-in Speakers (default)\\n  7 HDMI Speakers\\n' ;;
  *) exit 1 ;;
esac
""")
    (root / "ascii-chat").chmod(0o755)
    (root / "sample video.mp4").touch()
    (root / ".zshrc").write_text(
        "autoload -Uz compinit\ncompinit -D\n"
        f"source {shlex.quote(str(script))}\n"
        "PS1='test> '\n"
        "bindkey -e\n"
        "zstyle ':completion:*' list-colors ''\n"
        "zstyle ':completion:*' menu no\n"
        f"path=({shlex.quote(directory)} $path)\n"
        f"cd {shlex.quote(directory)}\n"
    )
    try:
        tmux("new-session", "-d", "-s", "test", "-x", "130", "-y", "42",
             f"env ZDOTDIR={shlex.quote(directory)} zsh -i")
        time.sleep(1)
        cases = [
            ("ascii-chat --color-mode ", ["truecolor", "mono", "Monochrome only"], []),
            ("ascii-chat mirror --render-mode ", ["foreground", "2x vertical resolution"], []),
            ("ascii-chat mirror --render-mode=", ["foreground", "background"], []),
            ("ascii-chat --log-level debug mirror --render-mode ", ["foreground", "background"], []),
            ("ascii-chat --log-level=debug mirror --render-mode ", ["foreground", "background"], []),
            ("ascii-chat mirror -M ", ["foreground", "background"], []),
            ("ascii-chat mirror --splash-screen ", ["true", "false", "enable", "disable"], []),
            ("ascii-chat mirror --splash-screen=", ["true", "false"], []),
            ("ascii-chat mirror --splash-screen --render-mode ", ["foreground", "background"], []),
            ("ascii-chat mirror --waveform --render-mode ", ["foreground", "background"], []),
            ("ascii-chat client --audio-source ", ["all", "call", "mic", "media"], []),
            ("ascii-chat mirror --audio-capture-source ", ["auto", "mic", "media", "both", "remote"], []),
            ("ascii-chat mirror --render-theme ", ["dark", "light", "auto"], []),
            ("ascii-chat --utf8 ", ["auto", "true", "false"], []),
            ("ascii-chat --color ", ["auto", "true", "false"], []),
            ("ascii-chat --completions ", ["bash", "fish", "zsh", "powershell"], []),
            ("ascii-chat mirror --palette ", ["standard", "blocks", "minimal"], []),
            ("ascii-chat mirror --color-filter ", ["rainbow", "cyan", "magenta"], []),
            ("ascii-chat mirror --webcam-index ", ["0", "2", "Integrated Camera", "USB Camera: HD"], []),
            ("ascii-chat --log-level debug client --microphone-index ", ["1", "4", "USB Microphone", "Desk Microphone"], []),
            ("ascii-chat client --speakers-index=", ["3", "7", "Built-in Speakers", "HDMI Speakers"], []),
            ("ascii-chat mirror --file sam", [r"sample\ video.mp4"], []),
            ("ascii-chat --log-file mirror --color-mode ", ["truecolor", "none"], []),
            ("ascii-chat server --status-screen ", ["true", "false"], []),
            ("ascii-chat discovery-service --status-screen ", ["true", "false"], []),
            ("ascii-chat acds --status-screen ", ["true", "false"], []),
            ("ascii-chat blue-mountain-tiger --render-mode ", ["foreground", "background"], []),
            ("ascii-chat mirror --help --render-mode ", ["foreground", "background"], []),
            ("ascii-chat mir", ["ascii-chat mirror"], []),
            ("ascii-chat mirror --render-mode fore", ["--render-mode foreground"], []),
        ]
        cases += [
            ("ascii-chat mirror --fps ", ["30", "60", "144"], []),
            ("ascii-chat mirror --seek ", ["0", "60", "3:45"], []),
            ("ascii-chat mirror --color-mode fore", [], ["foreground"]),
            ("ascii-chat mirror --render-file sam", [r"sample\ video.mp4"], []),
            ("ascii-chat --color --render-mode ", ["foreground", "background"], []),
        ]
        cases += [
            ("ascii-chat --log-level debug mir", ["debug mirror"], []),
            ("ascii-chat mirror -Mfore", ["-Mforeground"], []),
        ]
        failures = []
        captures = []
        for command, expected, forbidden in cases:
            tmux("send-keys", "-t", "test", "C-c")
            time.sleep(.1)
            tmux("send-keys", "-t", "test", "C-l")
            tmux("send-keys", "-t", "test", "-l", command)
            tmux("send-keys", "-t", "test", "Tab")
            time.sleep(.25)
            output = tmux("capture-pane", "-p", "-t", "test")
            captures.append(f"$ {command}<TAB>\n{output.rstrip()}\n")
            missing = [value for value in expected if value not in output]
            unexpected = [value for value in forbidden + ["bad pattern", "not a valid", "can only be called"] if value in output]
            if missing or unexpected:
                failures.append(command)
                print(f"FAIL {command!r}: missing={missing}, unexpected={unexpected}\n{output}")
            else:
                print(f"PASS {command!r}")
        if len(sys.argv) > 2:
            Path(sys.argv[2]).write_text("\n".join(captures))
        if failures:
            raise SystemExit(f"{len(failures)}/{len(cases)} failed")
        print(f"All {len(cases)} terminal completion checks passed.")
    finally:
        subprocess.run(["tmux", "-L", socket, "kill-server"], capture_output=True)
