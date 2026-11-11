"""Exercise native UI screens through real PTYs."""
import pytest
from ascii_chat_tests.terminal import capture_sync as capture

pytestmark = [pytest.mark.integration, pytest.mark.terminal]


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


@pytest.mark.parametrize("kind,label,answer", CASES)
def test_native_ui_screen(ui_native_probe, terminal_factory, tmp_path, kind, label, answer):
    root = tmp_path
    config = str(tmp_path)
    env = {"HOME": config, "USERPROFILE": config, "APPDATA": config, "XDG_CONFIG_HOME": config}
    term = terminal_factory([str(ui_native_probe), kind, str(root / (kind + ".log"))],
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
