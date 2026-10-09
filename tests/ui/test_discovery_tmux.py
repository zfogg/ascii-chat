"""Verify the native discovery invitation and call in real tmux terminals.

Run inside a Linux container with tmux installed:
  python tests/ui/test_discovery_tmux.py build/bin/ascii-chat build/tmux-validation
No mode or transport override is supplied to either discovery peer.
"""
import os
from pathlib import Path
import re
import shlex
import socket
import subprocess as sp
import sys
import time

EXE = Path(sys.argv[1]).resolve()
OUT = Path(sys.argv[2]).resolve()
OUT.mkdir(parents=True, exist_ok=True)
SOCKET = "ascii-invitation-" + str(os.getpid())


def tmux(*args):
    """Run tmux on a private server owned by this test."""
    return sp.check_output(["tmux", "-L", SOCKET, *args], text=True)


def port():
    """Choose a currently unused loopback TCP port."""
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def launch(name, args):
    """Start the native executable in its own 80 by 24 terminal and HOME."""
    home = OUT / name
    home.mkdir(exist_ok=True)
    command = ["env", "-u", "CLAUDECODE", "TERM=tmux-256color", "COLORTERM=truecolor",
               "ASCII_CHAT_QUESTION_PROMPT_RESPONSE=y;y;y;y", "HOME=" + str(home),
               str(EXE), "--log-file", str(OUT / (name + ".log")), *args]
    tmux("new-session", "-d", "-s", name, "-x", "80", "-y", "24", shlex.join(command))
    tmux("set-option", "-t", name, "status", "off")
    tmux("set-option", "-t", name, "remain-on-exit", "on")


def screen(name):
    """Read tmux's visible terminal cells, without ANSI escapes."""
    return tmux("capture-pane", "-p", "-t", name)


def capture(name, label):
    """Save both plain terminal cells and their exact SGR color attributes."""
    (OUT / (label + ".txt")).write_text(screen(name))
    (OUT / (label + ".ansi")).write_text(tmux("capture-pane", "-p", "-e", "-t", name))


def until(check, timeout=30):
    """Wait for observable terminal state under a bounded deadline."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.1)
    raise AssertionError("Timed out waiting for terminal state; inspect captures and logs in " + str(OUT))


def invitation():
    """Assert the instruction immediately follows the session string."""
    lines = [line.strip() for line in screen("host").splitlines()]
    command = next(line for line in lines if line.startswith("Run: ascii-chat "))
    session = command.removeprefix("Run: ascii-chat ")
    assert lines[lines.index(command) - 1] == session
    return session


def invitation_ready(session=None):
    """Wait for repaint after tmux reflows the old screen during a resize."""
    try:
        value = invitation()
        return session is None or value == session
    except (StopIteration, AssertionError):
        return False


def call_visible(name):
    """Require a populated call grid with neither invitation nor broken ANSI text."""
    text = screen(name)
    return ("|" in text and "Run: ascii-chat" not in text and "Connecting to session:" not in text
            and "Share this string" not in text and not re.search(r"\[[0-9]+;", text))


try:
    acds_port, ws_port = port(), port()
    launch("acds", ["discovery-service", "127.0.0.1", "--port", str(acds_port),
                    "--websocket-port", str(ws_port), "--database", str(OUT / "acds.db"), "--status-screen=false"])
    until(lambda: (OUT / "acds.log").exists() and "listening" in (OUT / "acds.log").read_text().lower())
    common = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_port),
              "--test-pattern", "--audio=false", "--fps", "10"]
    launch("host", [*common, "--port", str(port())])
    until(invitation_ready)
    session = invitation()
    time.sleep(2)
    assert invitation() == session
    capture("host", "waiting")
    tmux("resize-window", "-t", "host", "-x", "50", "-y", "12")
    time.sleep(1)
    until(lambda: len(screen("host").splitlines()) == 12 and "__" not in screen("host") and invitation_ready(session))
    assert invitation() == session
    capture("host", "waiting-small")
    tmux("resize-window", "-t", "host", "-x", "80", "-y", "24")
    time.sleep(1)
    until(lambda: len(screen("host").splitlines()) == 24 and "__" in screen("host") and invitation_ready(session))
    capture("host", "waiting-restored")
    print("PASS: centered invitation, adjacent Run line, resize and restore", flush=True)

    launch("peer", [session, *common, "--port", str(port())])
    until(lambda: call_visible("host") and call_visible("peer"))
    observed = {"host": set(), "peer": set()}
    for _ in range(15):
        for name in observed:
            assert call_visible(name), name + " lost call output"
            assert tmux("display-message", "-p", "-t", name, "#{pane_dead}").strip() == "0"
            observed[name].add(screen(name))
        time.sleep(1)
    for name, frames in observed.items():
        assert len(frames) >= 3, name + " frame output froze"
        capture(name, name + "-connected")
    print("PASS: default session-string join; both call grids animate for 15s without splash remnants", flush=True)
finally:
    for name in ("host", "peer", "acds"):
        try:
            capture(name, name + "-final")
            tmux("send-keys", "-t", name, "C-c")
        except sp.CalledProcessError:
            pass
    sp.run(["tmux", "-L", SOCKET, "kill-server"], capture_output=True)
