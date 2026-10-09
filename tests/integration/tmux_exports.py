#!/usr/bin/env python3
"""Every advertised render-file extension in tmux, plus redirected snapshots."""
import argparse
import json
from pathlib import Path
import subprocess
import time
from tmux_rendering import Pane


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--artifacts", type=Path, required=True)
    args = ap.parse_args()
    root = args.artifacts.resolve()
    binary = str(args.binary.resolve())
    results = []
    mirror = ["mirror", "--file", str(root / "media.mp4"), "--audio=false", "--splash-screen=false", "--snapshot", "--snapshot-delay", "1", "--width", "40", "--height", "20", "--fps", "10"]
    for extension in ["mp4", "mov", "webm", "avi", "gif", "png", "jpg"]:
        name = "export-" + extension
        output = root / (name + "." + extension)
        output.unlink(missing_ok=True)
        p = None
        try:
            p = Pane(name, [binary, "--no-check-update", "--log-file", str(root / name / "application.log"), "--log-level", "warn", *mirror, "--render-file", str(output)], root, 80, 30)
            deadline = time.monotonic() + 45
            while not (p.directory / "exit").exists() and time.monotonic() < deadline:
                time.sleep(.2)
            assert (p.directory / "exit").exists(), "export did not finish"
            p.capture("completed")
            metadata = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(output)], text=True))
            streams = metadata["streams"]
            assert streams and streams[0]["width"] > 0 and streams[0]["height"] > 0
            results.append(dict(case=name, status="pass", codec=streams[0]["codec_name"], bytes=output.stat().st_size))
        except Exception as error:
            results.append(dict(case=name, status="fail", error=str(error)))
        finally:
            if p:
                try:
                    p.close()
                except Exception as error:
                    results[-1].update(status="fail", shutdown_error=str(error))
        print(results[-1]["status"].upper(), name, flush=True)
        (root / "export-results.json").write_text(json.dumps(results, indent=2))
    for render in ["foreground", "background", "half-block"]:
        name = "snapshot-" + render
        try:
            result = subprocess.run([binary, "--no-check-update", "--log-file", str(root / (name + ".log")), "--log-level", "warn", *mirror, "--render-mode", render, "--color-mode", "none", "--strip-ansi"], capture_output=True, timeout=30)
            (root / (name + ".txt")).write_bytes(result.stdout)
            assert result.returncode == 0 and result.stdout.strip(), result.stderr[-2000:]
            assert b"Terminal too small" not in result.stdout and b"\x1b" not in result.stdout
            results.append(dict(case=name, status="pass", scope="redirected output"))
        except Exception as error:
            results.append(dict(case=name, status="fail", error=str(error)))
        print(results[-1]["status"].upper(), name, flush=True)
        (root / "export-results.json").write_text(json.dumps(results, indent=2))
    raise SystemExit(any(r["status"] == "fail" for r in results))


if __name__ == "__main__":
    main()
