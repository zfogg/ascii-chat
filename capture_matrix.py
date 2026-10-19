"""Capture the actual application through a Unix PTY; preserve ANSI and stderr."""
import argparse, errno, fcntl, hashlib, json, os, pathlib, platform, pty
import select, struct, subprocess, termios, time

p = argparse.ArgumentParser()
p.add_argument("binary")
p.add_argument("output")
p.add_argument("--backends", default="scalar,sse2,ssse3,avx2")
p.add_argument("--label", default=platform.node())
p.add_argument("--emulator")
args = p.parse_args()
outdir = pathlib.Path(args.output)
outdir.mkdir(parents=True, exist_ok=True)
binary = pathlib.Path(args.binary).resolve()
manifest = {"host": args.label, "platform": platform.platform(),
            "binary": str(binary), "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "source_sha": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
            "terminal": {"cols": 124, "rows": 44}, "cases": []}
libraries = {str(f.resolve()) for f in (binary.parent.parent / 'lib').glob('libasciichat*') if f.is_file()}
manifest['runtime_libraries'] = {f: hashlib.sha256(pathlib.Path(f).read_bytes()).hexdigest() for f in sorted(libraries)}
for backend in args.backends.split(","):
    for pattern in (0, 1):
        for render in ("foreground", "background", "half-block"):
            for color in ("mono", "16", "256", "truecolor"):
                name = f"{backend}-pattern{pattern}-{render}-{color}"
                prefix = [args.emulator, '-cpu', 'max'] if args.emulator else []
                cmd = prefix + [str(binary), "--log-level", "error", "mirror", f"--test-pattern={pattern}",
                       "--snapshot", "--snapshot-delay=1.5", "--width=120", "--height=40",
                       f"--color-mode={color}", f"--render-mode={render}",
                       "--splash-screen=false", "--audio=false", "--flip-x=false", "--flip-y=false"]
                master, slave = pty.openpty()
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 44, 124, 0, 0))
                env = dict(os.environ, TERM="xterm-256color", COLORTERM="truecolor",
                           ASCII_CHAT_SIMD=backend, LANG="en_US.UTF-8", ASAN_OPTIONS="detect_leaks=0")
                start = time.monotonic()
                data = bytearray()
                with (outdir / (name + ".stderr")).open("wb") as errors:
                    process = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=errors, env=env)
                    os.close(slave)
                    timed_out = False
                    while True:
                        if time.monotonic() - start > 30:
                            process.kill()
                            timed_out = True
                        if select.select([master], [], [], 0.2)[0]:
                            try:
                                block = os.read(master, 65536)
                            except OSError as error:
                                if error.errno == errno.EIO:
                                    break
                                raise
                            if not block:
                                break
                            data.extend(block)
                        elif process.poll() is not None:
                            break
                    code = process.wait()
                os.close(master)
                (outdir / (name + ".ansi")).write_bytes(data)
                result = {"name": name, "backend": backend, "pattern": pattern, "render": render,
                          "color": color, "command": cmd, "exit": code, "timeout": timed_out,
                          "seconds": round(time.monotonic() - start, 3), "bytes": len(data),
                          "stderr_bytes": (outdir / (name + ".stderr")).stat().st_size}
                manifest["cases"].append(result)
                (outdir / "manifest.json").write_text(json.dumps(manifest, indent=2))
                print(name, "exit", code, "bytes", len(data), flush=True)
                if code or timed_out or not data:
                    raise SystemExit("Capture failed: " + name)
