#!/usr/bin/env python3
"""Real mDNS advertisements, terminal navigation, and same-address reconnect.

Run with a native binary and pyte/pexpect (pywinpty on Windows). --artifacts
captures terminal cells and raw ANSI externally; Pillow adds PNG captures.
"""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import os
from pathlib import Path
import re
import shutil
import socket
import sys
import tempfile
import time

try:
    from terminal_ui import Terminal, load_terminal_font
except ImportError as error:
    print(f"SKIP: terminal prerequisites unavailable: {error}")
    raise SystemExit(77)
from server_lifecycle import Service


def service_port_pairs(count):
    """Reserve distinct TCP and WebSocket candidates for the whole server matrix."""
    reservations = []
    try:
        for _ in range(count * 2):
            reservation = socket.socket()
            reservations.append(reservation)
            reservation.bind(("127.0.0.1", 0))
        ports = [reservation.getsockname()[1] for reservation in reservations]
        return list(zip(ports[::2], ports[1::2]))
    finally:
        for reservation in reservations:
            reservation.close()


def capture(term, directory, name):
    if directory is None:
        return
    directory.mkdir(parents=True, exist_ok=True)
    (directory / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (directory / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return
    configured_font = os.environ.get("TERMINAL_CAPTURE_FONT")
    font = ImageFont.truetype(configured_font, 16) if configured_font else load_terminal_font(16)
    divider_mask = font.getmask("━")
    if not divider_mask.getbbox():
        raise AssertionError("Terminal capture font does not contain the UTF-8 divider glyph")
    image = Image.new("RGB", (term.screen.columns * 10 + 24, term.screen.lines * 21 + 24), "#10151c")
    draw = ImageDraw.Draw(image)
    for row, text in enumerate(term.screen.display):
        for column, character in enumerate(text):
            foreground = term.screen.buffer[row][column].fg
            color = f"#{foreground}" if re.fullmatch(r"[0-9a-fA-F]{6}", foreground) else "#e1e8ed"
            draw.text((12 + column * 10, 12 + row * 21), character, font=font, fill=color)
    image.save(directory / f"{name}.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-mdns-") as temporary:
        root = Path(temporary)
        for key in list(os.environ):
            if key.startswith("ASCII_CHAT_"):
                del os.environ[key]
        os.environ.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root),
                          XDG_CONFIG_HOME=str(root), TERM="xterm-256color",
                          ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")
        port_pairs = service_port_pairs(14)
        ports = [pair[0] for pair in port_pairs]
        services, terminals = [], []

        def server(index, name):
            service = Service(binary, root, name, "server", ["0.0.0.0", "--port", str(ports[index]),
                "--websocket-port", str(port_pairs[index][1]), "--status-screen=false"])
            services.append(service)
            service.wait_for("Server accepting connections")
            try:
                service.wait_for("mDNS: Server advertised")
            except AssertionError as error:
                text = service.text()
                mdns_details = "\n".join(line for line in text.splitlines() if "mdns" in line.lower() or "bonjour" in line.lower())
                if sys.platform == "darwin" and any(f"Bonjour (error {code})" in text for code in (-65563, -65570)):
                    print("SKIP: Bonjour service is unavailable or local-network access is denied\n" + mdns_details)
                    raise SystemExit(77) from error
                raise AssertionError("mDNS advertisement failed:\n" + mdns_details) from error
            return service

        def client(name, destination=None, utf8="true", color="true", scan=True):
            command = [str(binary), "--no-check-update", "--log-file", str(root / f"{name}.log"),
                "--log-level", "debug", "--color", color, "--utf8", utf8, "client"]
            if destination:
                command.append(destination)
            if scan:
                command.append("--scan")
            command += ["--test-pattern", "--audio=false", "--video-codec", "raw", "--fps", "5",
                        "--splash-screen=true", "--reconnect-attempts", "-1"]
            term = Terminal(command, rows=24, cols=110)
            terminals.append(term)
            return term

        def log_wait(name, message, timeout=20):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                path = root / f"{name}.log"
                text = path.read_text(errors="replace") if path.exists() else ""
                if message in text:
                    return text
                for term in terminals:
                    term.pump(.02)
            raise AssertionError(f"Missing {message!r}: {text[-3000:]}")

        try:
            first = server(0, "first")
            term = client("picker")
            term.expect(lambda text: "LAN servers" in text and str(ports[0]) in text, "--scan LAN picker missing")
            rows = term.screen.display
            occupied = [index for index, line in enumerate(rows) if line.strip()]
            assert abs(occupied[0] - (len(rows) - 1 - occupied[-1])) <= 2, rows
            for line in rows:
                if "LAN servers" in line or "> [1]" in line:
                    assert abs(len(line) - len(line.lstrip()) - (len(line) - len(line.rstrip()))) <= 2, line
            logo_row = occupied[0]
            colors = tuple(term.screen.buffer[logo_row][col].fg for col in range(term.screen.columns))
            term.pump(.3)
            animated = tuple(term.screen.buffer[logo_row][col].fg for col in range(term.screen.columns))
            assert colors != animated, "Rainbow logo did not animate"
            assert "Using test pattern mode" not in (root / "picker.log").read_text(errors="replace")
            capture(term, args.artifacts, "initial-server")
            second = server(1, "second")
            text = term.expect(lambda text: all(str(port) in text for port in ports[:2]),
                               "Background refresh omitted new server")
            second.stop()
            services.remove(second)
            term.expect(lambda text: str(ports[0]) in text and str(ports[1]) not in text,
                        "Background refresh retained a withdrawn server")
            second = server(1, "second-restarted")
            text = term.expect(lambda text: all(str(port) in text for port in ports[:2])
                               and re.search(r"> \[\d+\]", text) and "Auto-refresh" in text,
                               "Background refresh omitted the restarted server")
            assert "r: refresh" not in text
            assert "ascii-chat | LAN servers" not in text
            current = int(re.search(r"> \[(\d+)\]", text).group(1))
            term.pump(.2)
            term.write("j")
            expected = 2 if current == 1 else 1
            term.expect(lambda text: f"> [{expected}]" in text, "j did not move selection", timeout=5)
            if expected == 1:
                term.write("k")
                term.expect(lambda text: "> [2]" in text, "k did not wrap selection")
            # A PTY read can end after the selected row but before the footer.
            # Inspect colors only once all parts of the rendered frame arrived.
            term.expect(lambda text: "> [2]" in text and "Auto-refresh" in text
                        and sum("━" in line for line in text.splitlines()) == 2,
                        "Selection frame did not finish rendering its dividers and footer")
            rules = [row for row, line in enumerate(term.screen.display) if "━" in line]
            assert len(rules) == 2, "Header and footer must each have a divider"
            blue = next(term.screen.buffer[rules[0]][col].fg for col, char in enumerate(term.screen.display[rules[0]]) if char == "━")
            selected_row = next(row for row, line in enumerate(term.screen.display) if "> [2]" in line)
            selected_col = term.screen.display[selected_row].index(">")
            assert term.screen.buffer[selected_row][selected_col].fg == blue
            term.expect(lambda text: "Auto-refresh" in text, "LAN picker footer missing")
            footer_row = next(row for row, line in enumerate(term.screen.display) if "Auto-refresh" in line)
            footer = term.screen.display[footer_row]
            slash = footer.index("/")
            assert term.screen.buffer[footer_row][slash].fg == term.screen.buffer[footer_row][footer.index("move")].fg
            assert term.screen.buffer[footer_row][slash - 1].fg != term.screen.buffer[footer_row][slash].fg
            selected_port = next(port for port in ports[:2]
                                 if str(port) in next(line for line in term.screen.display if "> [2]" in line))
            term.pump(1.8)
            term.expect(lambda text: re.search(r"> \[\d+\]", text) and "Auto-refresh" in text,
                        "Background refresh did not finish drawing its selected row and footer")
            assert str(selected_port) in next(line for line in term.screen.display if "> [2]" in line), "Background refresh moved the selection"
            capture(term, args.artifacts, "highlighted-selection")
            term.write("k")
            term.expect(lambda text: "> [1]" in text, "k did not move selection")
            term.write("\x1b[B")
            term.expect(lambda text: "> [2]" in text, "Down arrow did not move selection")
            term.resize(4, 44)
            term.expect(lambda text: "> [2]" in text, "Selected row lost on resize")
            capture(term, args.artifacts, "small-terminal")
            term.resize(2, 20)
            term.expect(lambda text: "too small" in text.lower(), "Tiny terminal warning missing")
            term.write("\r")
            term.pump(.2)
            term.resize(24, 110)
            text = term.expect(lambda text: "> [2]" in text, "Covered Enter selected a server")
            chosen_line = next(line for line in text.splitlines() if "> [2]" in line)
            chosen = next(index for index, port in enumerate(ports[:2]) if str(port) in chosen_line)
            selected_host = chosen_line.rsplit("(", 1)[1].rsplit(":", 1)[0].strip("[]")
            term.write("\r")
            log_wait("picker", "Selected server")
            selected_service = [first, second][chosen]
            selected_service.wait_for("announced video stream")
            selected_service.stop()
            services.remove(selected_service)
            log_wait("picker", "Connection failed, retrying")
            text = term.expect(lambda text: selected_host in text
                               and ("Connecting to" in text or "Failed to connect to" in text)
                               and "LAN servers" not in text, "Reconnect UI lost the selected address")
            capture(term, args.artifacts, "reconnect-state")
            replacement = server(chosen, "replacement")
            replacement.wait_for("announced video stream")
            assert (root / "picker.log").read_text(errors="replace").count("Selected server") == 1
            term.interrupt()
            term.close()
            terminals.remove(term)
            print("PASS explicit --scan discovery, refresh, arrows/j/k, resize/covered input, selection and same-address reconnect")

            cancelled = client("cancelled", utf8="false", color="false")
            cancelled.expect(lambda text: "LAN servers" in text
                             and sum(line.strip().startswith("---") for line in text.splitlines()) == 2,
                             "Cancellation picker missing its completed ASCII dividers")
            assert "━" not in "\n".join(cancelled.screen.display)
            assert sum(line.strip().startswith("---") for line in cancelled.screen.display) == 2
            capture(cancelled, args.artifacts, "ascii-fallback")
            cancelled.write("q")
            cancelled.expect(lambda text: not cancelled.process.isalive(), "Cancellation did not exit")
            assert "Using test pattern mode" not in (root / "cancelled.log").read_text(errors="replace")
            default = client("default-no-scan", scan=False)
            log_wait("default-no-scan", "Using test pattern mode")
            default.pump(.3)
            assert "LAN servers" not in "".join(default.raw), "Default client unexpectedly opened LAN picker"
            default.interrupt()
            default.close()
            terminals.remove(default)
            before_explicit = replacement.text().count("announced video stream")
            explicit = client("explicit", f"127.0.0.1:{ports[chosen]}")
            deadline = time.monotonic() + 20
            while replacement.text().count("announced video stream") <= before_explicit:
                if time.monotonic() >= deadline:
                    raise AssertionError("Explicit client did not join the replacement server")
                explicit.pump(.05)
            log_wait("explicit", "Using test pattern mode")
            assert "LAN servers" not in "".join(explicit.raw)
            explicit.interrupt()
            print("PASS cancellation precedes media; explicit destination bypasses LAN picker")

            for index in range(2, 14):
                server(index, f"boundary-{index + 1}")
            boundary = client("boundary")
            boundary.expect(lambda text: "LAN servers" in text and " [12]" in text,
                            "Twelve-row LAN viewport did not fill")
            boundary.pump(2)
            text = boundary.expect(lambda text: re.search(r"> \[\d+\]", text)
                                   and "Auto-refresh" in text,
                                   "Boundary picker did not finish drawing its selected row and footer")
            # Refresh sorts discoveries while preserving the selected endpoint,
            # so its row need not remain one after the initial discovery query.
            current = int(re.search(r"> \[(\d+)\]", text).group(1))
            if current > 1:
                boundary.write("k" * (current - 1))
                boundary.expect(lambda text: "> [1]" in text,
                                "Could not establish the first row before testing thirteen Down events")
            boundary.write("j" * 13)
            text = boundary.expect(lambda text: "> [14]" in text,
                                   "Selection could not move safely to server 14")
            visible = [line for line in text.splitlines() if re.search(r"[ >] \[\d+\]", line)]
            assert len(visible) == 12, f"Expected 12 visible servers, got {len(visible)}: {visible}"
            assert boundary.process.isalive(), "Picker crashed while moving to server 14"
            boundary.write("j")
            boundary.expect(lambda text: "> [1]" in text, "Down from the last server did not wrap to server 1")
            boundary.write("k")
            text = boundary.expect(lambda text: "> [14]" in text,
                                   "Up from server 1 produced an invalid negative selection")
            assert "[-1]" not in text
            assert boundary.process.isalive(), "Picker crashed at the negative selection boundary"
            capture(boundary, args.artifacts, "fourteen-server-scroll")
            boundary.write("q")
            boundary.expect(lambda text: not boundary.process.isalive(), "Boundary picker did not exit")
            print("PASS 12-row viewport scrolls selection to 14 and wraps above server 1 without a negative index")
        finally:
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                for path in root.glob("*.log"):
                    shutil.copyfile(path, args.artifacts / path.name)
            if sys.exc_info()[0] is not None:
                for path in root.glob("*.log"):
                    print(f"--- {path.name} ---\n{path.read_text(errors='replace')[-12000:]}")
            for term in terminals:
                term.close()
            for service in services:
                service.stop()


if __name__ == "__main__":
    main()
