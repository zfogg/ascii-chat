"""Verify sync navigation/registration in live mirror, client and discovery sessions.

Uses loopback services, generated video plus a 440 Hz audio track, and a real PTY.
Optional --artifacts exports the captured terminal cells as PNGs (requires Pillow),
plus their plain text and unmodified ANSI stream for inspection.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

from terminal_ui import Terminal
from server_lifecycle import Service, free_port, server_options


def capture(term, root, name):
    root.mkdir(parents=True, exist_ok=True)
    (root / f"{name}.txt").write_text("\n".join(term.screen.display), encoding="utf-8")
    (root / f"{name}.ansi").write_text("".join(term.raw), encoding="utf-8")
    from PIL import Image, ImageDraw, ImageFont
    font_path = ("C:/Windows/Fonts/consola.ttf" if os.name == "nt" else
                 "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf")
    font = ImageFont.truetype(font_path, 16)
    symbol_paths = ("C:/Windows/Fonts/seguisym.ttf",
                    "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf")
    symbol_font = next((ImageFont.truetype(path, 16) for path in symbol_paths if Path(path).exists()), font)
    cw, ch = round(font.getlength("M")), 21
    image = Image.new("RGB", (term.screen.columns * cw + 24, term.screen.lines * ch + 24), "#0d141a")
    draw = ImageDraw.Draw(image)
    palette = {"default": "#dedbd0", "black": "#000000", "red": "#e85d6f", "green": "#90e070",
               "brown": "#f0cc91", "blue": "#65ace1", "magenta": "#c8a0d8", "cyan": "#65ace1",
               "white": "#dedbd0"}
    def color(value):
        return palette.get(value, "#" + value if len(value) == 6 else "#dedbd0")
    for y in range(term.screen.lines):
        for x in range(term.screen.columns):
            cell = term.screen.buffer[y][x]
            fg, bg = color(cell.fg), "#0d141a" if cell.bg == "default" else color(cell.bg)
            if cell.reverse:
                fg, bg = bg, fg
            if bg != "#0d141a":
                draw.rectangle((12+x*cw, 12+y*ch, 12+(x+1)*cw, 12+(y+1)*ch), fill=bg)
            draw.text((12+x*cw, 12+y*ch), cell.data,
                      font=symbol_font if "❌" in cell.data else font, fill=fg)
    image.save(root / f"{name}.png")


def selected(text):
    return tuple(map(int, re.search(r"item (\d+)/(\d+)", text).groups()))


def check_colors(term, text):
    # ConPTY can deliver a new header before the footer. Check a complete frame
    # rather than the arbitrary point where a timed inventory capture stopped.
    text = expect_sync(term, lambda t: True, "Incomplete sync frame before palette checks")
    # Default log palette: blue rules, red zeroes, purple values, orange
    # filename, and yellow key names. Check the captured terminal cells.
    first_number = re.search(r"\d+ objects", text.splitlines()[0]).start()
    zero = text.splitlines()[1].index("0 mutexes")
    assert term.screen.buffer[0][first_number].fg == "c8a0d8"
    assert term.screen.buffer[1][zero].fg == "e85d6f"
    assert term.screen.buffer[3][0].fg == "65ace1"
    assert term.screen.buffer[term.screen.lines - 3][0].fg == "f09664"
    assert term.screen.buffer[term.screen.lines - 1][0].fg == "c8a0d8"


def expect_sync(term, predicate, description):
    def complete(text):
        lines = text.splitlines()
        return (len(lines) >= 8 and lines[-1].startswith("0/Esc") and
                lines[-4].startswith(("─", "-")) and ":" in lines[-3] and predicate(text))
    return term.expect(complete, description, timeout=30)


def wait_video(term, name):
    def video(text):
        assert term.process.isalive(), f"{name} exited before media output:\n{text}"
        return ("Sync primitives" not in text and "Terminal too small" not in text and
                "Memory Report" not in text and len(text.strip()) > 400 and
                not re.search(r"\[\d{2}:\d{2}:", text))
    return term.expect(video, f"{name}: no media output", timeout=60)


def check_activity(rows, artifacts, name):
    activity = {}
    for primitive in ("named_registry_lock", "audio_processing_wait_guard", "timer_manager", "colorscheme", "display",
                      "shared_media_buffer_pool", "debug_memory", "queue_not_empty", "ui_controller", "ui_input", "ffmpeg_prefetch"):
        matches = [list(map(float, row.split()[-4:-2])) for row in rows
                   if len(row.split()) > 1 and row.split()[1].startswith(primitive[:24])]
        if not matches and name != "mirror" and primitive in ("display", "queue_not_empty"):
            continue
        assert matches, (name, primitive)
        activity[primitive] = [max(values) for values in zip(*matches)]
    assert max(activity["named_registry_lock"]) < 15, activity
    assert max(activity["colorscheme"]) < 1, activity
    if name == "mirror":
        assert max(activity["display"]) < 150, activity
        # At 15 FPS, three pairs/frame average 45/s. A quarter-second
        # sampler can include five frames at a boundary (about 60/s).
        assert max(activity["ffmpeg_prefetch"]) < 65, activity
        assert max(activity["ui_controller"]) < 100, activity
        assert max(activity["ui_input"]) < 5, activity
        assert max(activity["audio_processing_wait_guard"]) < 100, activity
        assert max(activity["shared_media_buffer_pool"]) < 5, activity
        assert max(activity["debug_memory"]) < 1, activity
        assert max(activity["queue_not_empty"]) < 90, activity
    # The worker is signaled by devices and has a 10 ms fallback; each condition
    # wait releases/reacquires its mutex in addition to the outer lock/unlock.
    assert max(activity["audio_processing_wait_guard"]) < 700, activity
    # Local hot-path profiling must not allocate global named timer records.
    assert max(activity["timer_manager"]) < 1, activity
    if artifacts:
        (artifacts / f"{name}-activity.json").write_text(json.dumps(activity, indent=2), encoding="utf-8")
    print(f"PASS {name} steady lock/unlock rates: {activity}", flush=True)


def inspect(term, artifacts, name, required):
    # Prove rendering precedes the overlay, and continues when it closes.
    before = wait_video(term, name)
    term.expect(lambda t: t != before, f"{name}: video is static")
    if artifacts:
        capture(term, artifacts, name + "-video")
    term.write("0")
    text = expect_sync(term, lambda t: "Sync primitives" in t and "Live" in t, f"{name}: no sync overlay")
    total = selected(text)[1]
    # Visit every page even when the live mode has more than 128 objects.
    term.resize(144, 160)
    term.write("\x1b[H")
    text = expect_sync(term, lambda t: "item " in t and
                       f"page 1/{(selected(t)[1] + 127) // 128}" in t and
                       t.splitlines()[-1].startswith("0/Esc"),
                       f"{name}: no inventory after resize")
    # Let the rate sampler leave startup/resize activity behind.
    text = term.pump(3)
    pages = int(re.search(r"page 1/(\d+)", text).group(1))
    inventory = []
    for page in range(1, pages + 1):
        text = expect_sync(term, lambda t: f"page {page}/{pages}" in t, "Missing live inventory page")
        text = term.pump(0.5)
        inventory.extend(text.splitlines()[4:-4])
        if artifacts:
            capture(term, artifacts, f"{name}-inventory-{page}")
        if page < pages:
            term.write("\x1b[C")
    assert not any("pause_mutex." in row for row in inventory), name
    assert not any("seek_access_mutex." in row or "static_mutex." in row for row in inventory), name
    assert not any("audio_recording." in row for row in inventory), name
    assert any("media_paused." in row for row in inventory), name
    for primitive in required:
        assert primitive[:25] in "\n".join(inventory), (name, primitive, text)
    assert text.splitlines()[2].rstrip().endswith("Total Wait"), text
    for row in inventory:
        fields = row.split()
        if fields and fields[0].lstrip(">") == "cond":
            duration = fields[-1]
            if len(fields) > 1 and re.fullmatch(r"\d+d", fields[-2]):
                duration = " ".join(fields[-2:])
            assert re.fullmatch(r"\d+(?:\.\d+)?(?:ns|µs|ms|s)|\d+:\d{2}(?::\d{2})?|\d+d \d+:\d{2}|busy",
                                duration), row
        elif fields:
            assert fields[-1] == "-", row
    check_activity(inventory, artifacts, name)
    types = re.search(r"M=(\d+) RW=(\d+) C=(\d+) A=(\d+)", text)
    assert types and sum(map(int, types.groups())) == selected(text)[1], text
    # Both separators span columns with one cell per character.
    assert text.splitlines()[3].startswith("─" * 159), text
    check_colors(term, text)
    term.write("\x1b[H")
    expect_sync(term, lambda t: "item 1/" in t, "Home before resize")
    if name == "mirror":
        # Keep the full warmed inventory visible. Scanning smaller pages while
        # rates re-sort can move the target past the scan between key presses.
        text = expect_sync(term, lambda t: "debug_memory." in t, "Memory tracker missing from full inventory")
        term.pump(0.5)
        if artifacts:
            capture(term, artifacts, "mirror-hotspots")
        term.write("\x1b[H")
    term.resize(22, 160)
    expect_sync(term, lambda t: "page 1/" in t and "Terminal too small" not in t, "Resize did not recover")
    term.write("\x1b[B" * 6)
    expect_sync(term, lambda t: "item 7/" in t, "Seventh item not selected")
    term.write("\x1b[F")
    text = expect_sync(term, lambda t: (selected(t)[0] == selected(t)[1])
                       if "item " in t else False,
                       "End did not select last item")
    text = term.pump(0.5)
    assert text.splitlines()[-4].startswith("─" * 159), text
    if artifacts:
        capture(term, artifacts, name + "-end")
    if name == "mirror":
        # Keep every active row visible without flooding the PTY with 144-row
        # media redraws during the hidden interval. The first resumed sample
        # lasts only 250 ms, so it must not sit behind queued media output.
        term.resize(46, 160)
        term.write("\x1b[H")
        expect_sync(term, lambda t: "named_registry_lock." in t and
                    f"page 1/{(selected(t)[1] + 37) // 38}" in t, "Registry sample inventory")
        # The first resumed sample spans the hidden interval; subsequent samples
        # include the visible inspector's own 4 Hz registry snapshot.
        term.write("0")
        before = wait_video(term, name)
        term.expect(lambda t: t != before, "Hidden inspector stopped video")
        term.pump(10)
        term.write("0")
        deadline = time.monotonic() + 2
        hidden_rate = None
        while time.monotonic() < deadline:
            resumed = term.pump(0.01)
            row = next((r for r in resumed.splitlines() if "named_registry_lock." in r), None)
            if (row and "Live" in resumed and "STALE" not in resumed and
                    resumed.splitlines()[-1].startswith("0/Esc")):
                rates = list(map(float, row.split()[-4:-2]))
                if max(rates) < 2:
                    hidden_rate = rates
                    if artifacts:
                        capture(term, artifacts, "mirror-first-resumed-sample")
                    break
        assert hidden_rate is not None, resumed
        print(f"PASS mirror registry across hidden rendering interval: {hidden_rate}", flush=True)
        term.resize(22, 160)
        expect_sync(term, lambda t: "page 1/" in t and
                    f"page 1/{(selected(t)[1] + 13) // 14}" in t, "Restore navigation geometry")
    term.write("\x1b[H")
    expect_sync(term, lambda t: "item 1/" in t and t.splitlines()[4].startswith(">"), "Home did not select first item")
    term.write("\x1b[B" * 6)
    expect_sync(term, lambda t: "item 7/" in t, "Could not select seventh entry again")
    term.write("\x1b[H")
    text = expect_sync(term, lambda t: "item 1/" in t and t.splitlines()[4].startswith(">"), "Home failed on first page")
    text = term.pump(0.5)
    if artifacts:
        capture(term, artifacts, name + "-home")
    term.write("\x1b[B" * 14)
    expect_sync(term, lambda t: "page 2/" in t and "item 15/" in t, "Down did not cross page")
    term.write("\x1b[A")
    expect_sync(term, lambda t: "page 1/" in t and "item 14/" in t, "Up did not cross page")
    term.write("\x1b[C")
    expect_sync(term, lambda t: "page 2/" in t, "Right did not advance page")
    term.write("\x1b[D")
    expect_sync(term, lambda t: "page 1/" in t, "Left did not return page")
    term.write("0")
    before = term.expect(lambda t: "Sync primitives" not in t, "Overlay did not close")
    term.expect(lambda t: t != before, "Media did not resume")
    print(f"PASS {name}: {total} objects, inventory, navigation, resize and video recovery", flush=True)
    return {"objects": total, "types": dict(zip(("mutex", "rwlock", "cond", "atomic"), map(int, types.groups())))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path)
    subset = parser.add_mutually_exclusive_group()
    subset.add_argument("--network-only", action="store_true", help="Run only client/discovery loopback checks")
    subset.add_argument("--mirror-only", action="store_true", help="Run only mirror/color-policy checks")
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-sync-modes-") as temp:
        root = Path(temp)
        for suffix in ["ascii-chat", "AppData/Roaming/ascii-chat", ".config/ascii-chat"]:
            (root / suffix).mkdir(parents=True, exist_ok=True)
        media = root / "video.mkv"
        subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x120:rate=15",
                        "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "4",
                        "-c:v", "libx264", "-c:a", "pcm_s16le", str(media)], check=True)
        env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
        env.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root),
                   ASCII_CHAT_QUESTION_PROMPT_RESPONSE="y;y;y", ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK="1")
        old = os.environ.copy()
        os.environ.clear()
        os.environ.update(env)
        services, terminals, results = [], [], {}
        def terminal(name, argv, extra=()):
            colors = [] if "--color-mode" in extra else ["--color-mode", "truecolor"]
            # Keep the media stream small enough for ConPTY/pyte to drain before
            # the first resumed 250 ms rate sample is replaced. Source FPS and
            # audio processing are unchanged by these output dimensions.
            width, height = (60, 15) if name == "mirror" else (100, 30)
            term = Terminal([str(binary), "--no-check-update", "--log-file", str(root / f"{name}.log"),
                             "--log-level", "debug", "--grep", "CLIENT.AUDIO.RECV", "--grep", "FRAME_PRESENT",
                             "--grep", "FRAME_WRITE_TTY", "--grep", "KEYBOARD.INPUT", "--grep", "Discovery",
                             "--grep", "ERROR", "--grep", "WARN",
                             *argv, "--file", str(media), "--loop", "--audio=true",
                             "--audio-source", "media", "--splash-screen=false", *colors,
                             "--fps", "15", "--width", str(width), "--height", str(height),
                             *extra], rows=30, cols=160)
            terminals.append(term)
            return term
        try:
            if not args.network_only:
                mirror = terminal("mirror", ["mirror"], ["--utf8", "true"])
                results["mirror"] = inspect(mirror, args.artifacts, "mirror",
                                            ["capture.frame_serial", "display.first_frame", "worker_should_stop",
                                             "write_index_producer_position", "discard_pending"])
                mirror.interrupt()
                mirror.close()
                # Verify colored ASCII separators and the monochrome policy separately.
                ascii_term = terminal("ascii", ["mirror"], ["--utf8", "false"])
                wait_video(ascii_term, "ASCII mirror")
                ascii_term.write("0")
                text = expect_sync(ascii_term, lambda t: "Sync primitives" in t and "Live" in t, "ASCII overlay missing")
                assert text.splitlines()[3].startswith("-" * 159), text
                check_colors(ascii_term, text)
                if args.artifacts:
                    capture(ascii_term, args.artifacts, "mirror-ascii")
                ascii_term.interrupt()
                ascii_term.close()
                plain = terminal("plain", ["mirror"], ["--utf8", "false", "--color-mode", "none"])
                wait_video(plain, "Monochrome mirror")
                plain.write("0")
                text = expect_sync(plain, lambda t: "Sync primitives" in t and "Live" in t, "Monochrome overlay missing")
                if args.artifacts:
                    capture(plain, args.artifacts, "mirror-plain")
                assert all(cell.fg == "default" for row in plain.screen.buffer.values() for cell in row.values()), text
                plain.interrupt()
                plain.close()
                print("PASS colored ASCII fallback and --color-mode none", flush=True)
            if args.mirror_only:
                return
            tcp, ws, acds_tcp, acds_ws = [free_port() for _ in range(4)]
            identity = root / "acds-key"
            identity.write_bytes(bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"
                                              "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"))
            acds = Service(binary, root, "acds", "discovery-service", ["127.0.0.1", "--key", str(identity),
                           "--port", str(acds_tcp), "--websocket-port", str(acds_ws), "--database", str(root/"acds.db"),
                           "--status-screen=false", "--grep", "Server accepting connections|ERROR|WARN"], env)
            services.append(acds)
            acds.wait_for("Server accepting connections")
            discovery = ["--discovery-service", "127.0.0.1", "--discovery-service-port", str(acds_tcp)]
            host = Service(binary, root, "host", "server", server_options(tcp, ws) + discovery +
                           ["--discovery", "--webrtc", "--password", "sync-test-password", "--grep",
                            "Session created|state=changing|Server accepting connections|ERROR|WARN"], env)
            services.append(host)
            host.wait_for("Session created:")
            session = re.search(r"Session created: ([a-z]+-[a-z]+-[a-z]+)", host.text())[1]
            client = terminal("client", ["client", f"127.0.0.1:{tcp}", "--password", "sync-test-password"])
            wait_video(client, "client")
            # Join while the discovered session is fresh, before spending time
            # on the conventional client's navigation and resize checks.
            participant = terminal("discovery", [session, *discovery, "--no-webrtc", "--password", "sync-test-password"])
            wait_video(participant, "discovery")
            results["client"] = inspect(client, args.artifacts, "client",
                                        ["client.frames_rendered", "client.audio_sender_exited", "capture.frame_serial"])
            results["discovery"] = inspect(participant, args.artifacts, "discovery",
                                           ["discovery.media_running", "discovery.last_pong_received_ms", "capture.frame_serial"])
            client.pump(1)
            audio_log = (root / "client.log").read_text(encoding="utf-8", errors="replace")
            assert re.search(r"CLIENT AUDIO RECV #\d+: \d+ samples, RMS=0\.[0-9]*[1-9]", audio_log), audio_log[-4000:]
            assert "state=changing" in host.text(), host.text()[-4000:]
            print("PASS loopback clients exchange changing ASCII video and nonzero audio samples", flush=True)
        finally:
            for term in reversed(terminals):
                term.close()
            for service in reversed(services):
                service.stop()
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                for log in root.glob("*.log"):
                    (args.artifacts / log.name).write_bytes(log.read_bytes())
                (args.artifacts / "inventory.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            os.environ.clear()
            os.environ.update(old)


if __name__ == "__main__":
    main()
