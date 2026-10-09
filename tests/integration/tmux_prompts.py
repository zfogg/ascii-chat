#!/usr/bin/env python3
"""Exercise native prompt/TUI entry points in a real tmux PTY (synthetic identities)."""
import argparse
import json
import shlex
from pathlib import Path
import time
from tmux_rendering import Pane, tmux


CASES = [
    ("text", "Account name", "alice"),
    ("password", "Server password", "s3cret"),
    ("ssh-password", "Encrypted SSH", "s3cret"),
    ("gpg-password", "GPG key", "s3cret"),
    ("hidden-password", "Hidden password", "s3cret"),
    ("yes-default", "Default answer", ""),
    ("no-default", "Default answer", ""),
    ("cancel", "Cancellation", None),
    ("unknown-host", "192.0.2.1", "no"),
    ("unverified-host", "192.0.2.1", "no"),
    ("acds-key", "fixture.invalid", "no"),
    ("update-yes", "99.0.0", "y"),
    ("update-no", "99.0.0", "n"),
    ("mdns", "fixture-two", "2"),
    ("mdns-cancel", "fixture-two", ""),
    ("splash", "", ""),
    ("signal-waveform-mic", "", ""),
    ("signal-waveform-call", "", ""),
    ("signal-fft-mic", "", ""),
    ("signal-fft-call", "", ""),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    parser.add_argument("--filter", default="")
    parser.add_argument("--redirect-stderr", action="store_true")
    args = parser.parse_args()
    root = args.artifacts.resolve()
    results = []
    for kind, label, answer in CASES:
        if args.filter not in kind:
            continue
        name = "prompt-" + kind
        pane = None
        try:
            argv = [str(args.probe.resolve()), kind, str(root / name / "application.log")]
            if args.redirect_stderr:
                argv = ["sh", "-c", 'exec "$@" 2>' + shlex.quote(str(root / name / "stderr.log")), "prompt", *argv]
            pane = Pane(name, argv, root, 100, 45)
            raw = pane.directory / "terminal.raw"
            tmux("pipe-pane", "-t", pane.name, "cat > " + shlex.quote(str(raw)))
            pane.expect(lambda s: label in s and len(s.strip()) > 5, "normal")
            if kind in {"text", "password", "unknown-host"}:
                time.sleep(.2)
                before = raw.read_bytes().count(b"\x1b[2J")
                time.sleep(.3)
                assert raw.read_bytes().count(b"\x1b[2J") == before, "Idle prompt repeatedly clears the screen"
            pane.resize(20, 6)
            pane.expect(lambda s: "Terminal too small" in s, "small")
            pane.text("ignored")
            time.sleep(.2)
            pane.resize(100, 45)
            pane.expect(lambda s: "Terminal too small" not in s and label in s, "restored")
            time.sleep(.2)
            if kind == "splash" or kind.startswith("signal-"):
                pass
            elif answer is None:
                pane.key("Escape")
            else:
                if kind == "text":
                    pane.text("alixe")
                    pane.key("Left", "BSpace")
                    pane.text("c")
                    pane.key("End")
                    pane.key("BSpace", "BSpace")
                    pane.expect(lambda s: any(line.rstrip() == "> ali" for line in s.splitlines()), "deleted-tail")
                    pane.text("ce")
                else:
                    pane.text(answer)
                time.sleep(.2)
                entered = pane.capture("entered")
                assert "ignored" not in entered
                if "password" in kind:
                    assert "s3cret" not in entered
                    if kind != "hidden-password":
                        assert "******" in entered
                pane.key("Enter")
            pane.expect(lambda s: "PROBE PASSED" in s, "passed", timeout=20)
            deadline = time.monotonic() + 10
            while not (pane.directory / "exit").exists() and time.monotonic() < deadline:
                time.sleep(.1)
            results.append({"case": name, "status": "pass", "scope": "native API fixture"})
        except Exception as error:
            results.append({"case": name, "status": "fail", "error": str(error)})
        finally:
            if pane:
                try:
                    pane.close()
                except Exception as error:
                    results[-1].update(status="fail", shutdown_error=str(error))
        print(results[-1]["status"].upper(), name, flush=True)
        (root / "prompt-results.json").write_text(json.dumps(results, indent=2))
    raise SystemExit(any(r["status"] == "fail" for r in results))


if __name__ == "__main__":
    main()
