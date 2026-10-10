"""Native stats TUI acceptance; run in Linux with tmux and the built binary."""

import argparse
import json
from pathlib import Path
import re
import shlex
import socket
import subprocess
import time
import signal
import os

SOCKET = "stats273-acceptance"


def tmux(*args):
    return subprocess.check_output(["tmux", "-L", SOCKET, *map(str, args)], text=True)


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def wait(check, timeout=25):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        value = check()
        if value:
            return value
        time.sleep(0.1)
    raise AssertionError("Timed out waiting for terminal state")


class Pane:
    def __init__(self, name, args, binary, root):
        self.name = name
        self.root = root
        self.exit = root / (name + ".exit")
        self.exit.unlink(missing_ok=True)
        (root / (name + ".log")).unlink(missing_ok=True)
        home = root / (name + "-home")
        home.mkdir(exist_ok=True)
        argv = [
            "env",
            "-u",
            "CLAUDECODE",
            "ASAN_OPTIONS=detect_leaks=0",
            "UBSAN_OPTIONS=halt_on_error=1",
            "ASCII_CHAT_QUESTION_PROMPT_RESPONSE=y;y;y;y;y;y",
            "HOME=" + str(home),
            binary,
            "--no-check-update",
            "--log-level",
            "info",
            "--log-file",
            str(root / (name + ".log")),
            *args,
        ]
        cmd = (
            shlex.join(argv)
            + "; code=$?; printf '%s' \"$code\" > "
            + shlex.quote(str(self.exit))
            + "; exec sleep 600"
        )
        tmux("new-session", "-d", "-s", name, "-x", 110, "-y", 54, "bash", "-c", cmd)
        tmux("set-option", "-t", name, "status", "off")

    def text(self):
        return tmux("capture-pane", "-p", "-t", self.name)

    def key(self, *keys):
        tmux("send-keys", "-t", self.name, *keys)

    def capture(self, label):
        (self.root / (label + ".txt")).write_text(self.text())
        (self.root / (label + ".ansi")).write_text(
            tmux("capture-pane", "-e", "-p", "-t", self.name)
        )

    def open(self):
        self.key("=")
        wait(lambda: "LIVE STATS" in self.text())
        time.sleep(1.5)

    def close(self):
        self.key("C-c")
        wait(self.exit.exists, 40)
        self.capture(self.name + "-exit")
        code = self.exit.read_text()
        log = (self.root / (self.name + ".log")).read_text(errors="replace")
        assert (
            "runtime error:" not in log and "ERROR: AddressSanitizer" not in log
        ), self.name
        assert code in (
            ("0", "1", "130") if self.name in ("client", "websocket") else ("0", "130")
        ), (self.name, code)
        tmux("kill-session", "-t", self.name)


def metric(pane, name):
    m = re.search(r"^\s*" + name + r"\s+(\d+)\s+([\d.]+)", pane.text(), re.M)
    return (int(m[1]), float(m[2])) if m else None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--artifacts", required=True)
    args = parser.parse_args()
    root = Path(args.artifacts).resolve()
    root.mkdir(parents=True, exist_ok=True)
    binary = str(Path(args.binary).resolve())
    panes = []
    results = []
    tmux("new-session", "-d", "-s", "keeper")

    def launch(name, argv):
        p = Pane(name, argv, binary, root)
        panes.append(p)
        return p

    try:
        mirror = launch(
            "mirror",
            [
                "mirror",
                "--test-pattern",
                "--audio=false",
                "--splash-screen=false",
                "--fps",
                "30",
            ],
        )
        wait(
            lambda: (root / "mirror.log").exists()
            and (root / "mirror.log")
            .read_text(errors="replace")
            .count("PIPELINE_MAIN_RENDER")
            >= 5
        )
        time.sleep(1)
        mirror.open()
        first = metric(mirror, "frames_captured")
        assert first, mirror.text()
        time.sleep(1.5)
        assert metric(mirror, "frames_captured")[0] > first[0]
        assert metric(mirror, "frames_presented")[1] == 0, mirror.text()
        mirror.capture("mirror-overview")
        mirror.key("Tab")
        wait(lambda: "CONNECTIONS & MODE DETAILS" in mirror.text())
        mirror.capture("mirror-details")
        tmux("resize-window", "-t", "mirror", "-x", 60, "-y", 12)
        time.sleep(0.5)
        mirror.capture("mirror-narrow")
        tmux("resize-window", "-t", "mirror", "-x", 40, "-y", 8)
        wait(lambda: "Terminal too small" in mirror.text())
        tmux("resize-window", "-t", "mirror", "-x", 110, "-y", 54)
        wait(lambda: "LIVE STATS" in mirror.text())
        mirror.key("Escape")
        wait(lambda: "LIVE STATS" not in mirror.text())
        mirror.key("?")
        wait(lambda: "Keyboard Shortcuts" in mirror.text())
        mirror.key("=")
        time.sleep(0.3)
        assert "LIVE STATS" not in mirror.text(), "Stats stole help input"
        mirror.key("?")
        time.sleep(0.5)
        mirror.open()
        mirror.capture("mirror-reopened")
        results.append(
            {
                "mode": "mirror",
                "continued_capture": True,
                "overlay_presentation_fps": 0,
                "resize_close_help": "pass",
            }
        )
        sp, wp = port(), port()
        server = launch(
            "server",
            [
                "server",
                "127.0.0.1",
                "--port",
                str(sp),
                "--websocket-port",
                str(wp),
                "--status-screen=false",
                "--stats-interval",
                "1",
            ],
        )
        wait(lambda: "stats mode=server" in server.text())
        client = launch(
            "client",
            [
                "client",
                "127.0.0.1:" + str(sp),
                "--test-pattern",
                "--audio=false",
                "--splash-screen=false",
                "--fps",
                "20",
            ],
        )
        time.sleep(5)
        client.open()
        wait(
            lambda: metric(client, "frames_received")
            and metric(client, "frames_received")[0] > 0
        )
        client.capture("client-overview")
        server.open()
        wait(
            lambda: metric(server, "frames_sent")
            and metric(server, "frames_sent")[1] > 0
        )
        # Reusing a consumed frame must not count as an unread overwrite.
        received = metric(server, "frames_received")[0]
        assert metric(server, "queue_drops")[0] < received / 2, server.text()
        server.capture("server-overview")
        server.key("Tab")
        wait(lambda: "SERVER CLIENTS" in server.text())
        server.capture("server-details")
        server.key("Escape")
        wait(lambda: "stats mode=server" in server.text())
        server.open()
        results.append(
            {
                "mode": "server/client",
                "tcp_media": "pass",
                "headless_toggle_and_stdout_restore": "pass",
            }
        )
        # A second real client exercises the WebSocket path.
        ws = launch(
            "websocket",
            [
                "client",
                "ws://127.0.0.1:" + str(wp),
                "--test-pattern",
                "--audio=false",
                "--splash-screen=false",
                "--fps",
                "20",
            ],
        )
        time.sleep(5)
        ws.open()
        wait(
            lambda: metric(ws, "frames_received")
            and metric(ws, "frames_received")[0] > 0
        )
        ws.key("Tab")
        wait(lambda: "WebSocket" in ws.text())
        ws.capture("websocket-details")
        results.append({"transport": "WebSocket", "media": "pass"})
        ap, awp = port(), port()
        acds = launch(
            "acds",
            [
                "discovery-service",
                "127.0.0.1",
                "--port",
                str(ap),
                "--websocket-port",
                str(awp),
                "--database",
                str(root / "sessions.db"),
                "--status-screen=false",
                "--stats-interval",
                "1",
            ],
        )
        wait(lambda: "stats mode=discovery-service" in acds.text())
        common = [
            "--discovery-service",
            "127.0.0.1",
            "--discovery-service-port",
            str(ap),
            "--test-pattern",
            "--audio=false",
            "--fps",
            "20",
            "--prefer-webrtc",
            "--webrtc-skip-stun",
            "--webrtc-disable-turn",
        ]
        host = launch("discovery", [*common, "--port", str(port())])

        def session():
            m = re.search(r"Run: ascii-chat ([a-z]+-[a-z]+-[a-z]+)", host.text())
            return m[1] if m else None

        session_name = wait(session, 40)
        participant = launch("participant", [session_name, *common])
        wait(
            lambda: (root / "participant.log").exists()
            and "DataChannel opened"
            in (root / "participant.log").read_text(errors="replace"),
            45,
        )
        time.sleep(3)
        host.open()
        wait(
            lambda: metric(host, "frames_received")
            and metric(host, "frames_received")[1] > 0
        )
        host.capture("discovery-overview")
        host.key("Tab")
        wait(lambda: "CONNECTIONS & MODE DETAILS" in host.text())
        host.capture("discovery-details")
        acds.open()
        acds.capture("acds-overview")
        acds.key("Tab")
        wait(lambda: session_name in acds.text())
        acds.capture("acds-sessions")
        results.append(
            {"mode": "discovery/acds", "session": session_name, "session_table": "pass"}
        )
        status = launch(
            "status",
            [
                "server",
                "127.0.0.1",
                "--port",
                str(port()),
                "--websocket-port",
                str(port()),
                "--status-screen=true",
            ],
        )
        time.sleep(4)
        status.key("/")
        time.sleep(0.5)
        status.key("=")
        time.sleep(0.5)
        assert "LIVE STATS" not in status.text(), "Stats stole grep input"
        status.key("Escape")
        time.sleep(0.5)
        status.open()
        status.capture("server-status-overlay")
        results.append({"input": "interactive grep retains equals", "result": "pass"})
        for mode in ("server", "discovery-service"):
            outfile = root / (mode + "-stdout.txt")
            with outfile.open("w") as output:
                argv = [
                    binary,
                    "--no-check-update",
                    "--log-file",
                    str(root / (mode + "-pipe.log")),
                    mode,
                    "127.0.0.1",
                    "--port",
                    str(port()),
                    "--websocket-port",
                    str(port()),
                    "--status-screen=false",
                    "--stats-interval",
                    "1",
                ]
                if mode == "discovery-service":
                    argv += ["--database", str(root / "pipe.db")]
                env = os.environ.copy()
                env["ASAN_OPTIONS"] = "detect_leaks=0"
                process = subprocess.Popen(
                    argv,
                    stdin=subprocess.DEVNULL,
                    stdout=output,
                    stderr=subprocess.DEVNULL,
                    env=env,
                )
                try:
                    wait(lambda: "stats mode=" in outfile.read_text())
                finally:
                    process.send_signal(signal.SIGINT)
                    process.wait(timeout=15)
            lines = [
                line
                for line in outfile.read_text().splitlines()
                if line.startswith("stats mode=")
            ]
            assert lines and all(
                "\x1b" not in line and "mean_ms=" in line for line in lines
            )
            results.append(
                {"mode": mode, "redirected_stdout": "ANSI-free summaries with timings"}
            )
        (root / "results.json").write_text(json.dumps(results, indent=2))
        print(json.dumps(results, indent=2))
    finally:
        failures = []
        for pane in reversed(panes):
            try:
                pane.close()
            except Exception as e:
                pane.capture(pane.name + "-failure")
                failures.append(str(e))
        if failures:
            raise AssertionError("Cleanup failures: " + repr(failures))
        try:
            tmux("kill-session", "-t", "keeper")
        except subprocess.CalledProcessError:
            pass


if __name__ == "__main__":
    main()
