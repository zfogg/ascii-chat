"""Exercise shared prompt, popup, button and list screens in native PTYs."""
import argparse
from pathlib import Path
import tempfile
from terminal_ui import Terminal
from sync_tui_modes import capture

CASES = [
    ("render-progress", "Rendering file", None),
    ("media-loading", "Loading media...", None),
    ("notice", "Example warning", None),
    ("text", "Account name", "alice\r"),
    ("password", "Server password", "s3cret\r"),
    ("ssh-password", "Encrypted SSH", "s3cret\r"),
    ("gpg-password", "GPG key", "s3cret\r"),
    ("hidden-password", "Hidden password", "s3cret\r"),
    ("yes-default", "Default answer", "\r"),
    ("no-default", "Default answer", "\r"),
    ("cancel", "Cancellation", "\x1b"),
    ("unknown-host", "192.0.2.1", "no\r"),
    ("unverified-host", "192.0.2.1", "no\r"),
    ("acds-key", "fixture.invalid", "no\r"),
    ("update-no", "99.0.0", "n"),
    ("mdns", "fixture-two", "2\r"),
    ("mdns-long", "fixture-01", "10\r"),
    ("mdns-cancel", "fixture-two", "\x1b"),
    ("invitation", "fixture-tiger-cloud", None),
    ("splash", "Video chat in your terminal", None),
]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", required=True, type=Path)
    parser.add_argument("--artifacts", required=True, type=Path)
    parser.add_argument("--filter", default="")
    args = parser.parse_args()
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ascii-native-ui-") as config:
        env = {"HOME": config, "USERPROFILE": config, "APPDATA": config, "XDG_CONFIG_HOME": config}
        for kind, label, answer in CASES:
            if args.filter and args.filter not in kind:
                continue
            term = Terminal([str(args.probe.resolve()), kind, str(root / (kind + ".log"))],
                            rows=48, cols=120, environment=env)
            try:
                term.expect(lambda text: label.lower() in text.lower(), kind + " did not render", timeout=25)
                capture(term, root, kind)
                if kind.startswith("mdns"):
                    term.write("?")
                    term.expect(lambda text: "LAN Selection Keyboard Shortcuts" in text, "LAN help did not open")
                    capture(term, root, kind + "-help")
                    term.write("?")
                    term.expect(lambda text: "LAN Selection Keyboard Shortcuts" not in text and label in text,
                                "LAN help did not restore selection")
                if answer is not None:
                    term.resize(2 if kind.startswith("mdns") else 6, 20)
                    term.expect(lambda text: "Terminal too small" in text, kind + " minimum geometry")
                    capture(term, root, kind + "-small")
                    term.resize(48, 120)
                    term.expect(lambda text: label.lower() in text.lower() and "Terminal too small" not in text,
                                kind + " resize restoration")
                    if kind == "mdns-long":
                        term.write("\x1b[F")
                        term.expect(lambda text: "fixture-24" in text and "fixture-01" not in text,
                                    "LAN table did not scroll to its last row")
                        capture(term, root, "mdns-long-scrolled")
                    term.write(answer)
                term.expect(lambda text: "PROBE PASSED" in text, kind + " did not complete", timeout=30)
                print("PASS", kind, flush=True)
            except BaseException:
                capture(term, root, kind + "-failure")
                raise
            finally:
                term.close()

if __name__ == "__main__":
    main()
