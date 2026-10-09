#!/usr/bin/env python3
"""Loopback authentication prompts and encrypted test-key loading in tmux."""
import argparse
import json
from pathlib import Path
import subprocess
import time
from tmux_rendering import Pane
from tmux_flows import port


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--artifacts", type=Path, required=True)
    args = ap.parse_args()
    root, binary = args.artifacts.resolve(), str(args.binary.resolve())
    for name, password in [("identity", ""), ("encrypted", "s3cretpass")]:
        key = root / name
        if not key.exists():
            subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", password, "-f", str(key)], check=True)
    results = []

    def pane(name, flags):
        return Pane(name, [binary, "--no-check-update", "--log-file", str(root / name / "application.log"), "--log-level", "warn", *flags], root, 100, 45)

    def prompt(p, label, response):
        p.expect(lambda s: label in s, "prompt")
        p.resize(19, 6)
        p.expect(lambda s: "Terminal too small" in s, "small")
        p.text("discard")
        time.sleep(.2)
        p.resize(100, 45)
        p.expect(lambda s: label in s and "Terminal too small" not in s, "restored")
        time.sleep(.2)
        p.text(response)
        time.sleep(.2)
        screen = p.capture("entered")
        assert "discard" not in screen and "s3cretpass" not in screen
        p.key("Enter")

    for kind in ["encrypted-ssh", "server-password", "missing-client-key", "trusted-host"]:
        sessions = []
        try:
            tcp = port()
            flags = ["server", "127.0.0.1", "--port", str(tcp), "--websocket-port", str(port()), "--status-screen"]
            if kind == "encrypted-ssh":
                flags += ["--key", str(root / "encrypted")]
            elif kind == "server-password":
                flags += ["--password", "s3cretpass"]
            else:
                flags += ["--key", str(root / "identity")]
                if kind == "missing-client-key":
                    flags += ["--client-keys", str(root / "identity.pub")]
            server = pane("auth-" + kind + "-server", flags)
            sessions.append(server)
            if kind == "encrypted-ssh":
                prompt(server, "Encrypted SSH key", "s3cretpass")
            server.expect(lambda s: "ascii-chat Server" in s, "running", timeout=25)
            if kind != "encrypted-ssh":
                flags = ["client", "127.0.0.1", "--port", str(tcp), "--file", str(root / "media.mp4"), "--loop", "--audio=false", "--splash-screen=false"]
                if kind == "missing-client-key":
                    flags += ["--server-key", str(root / "identity.pub")]
                client = pane("auth-" + kind + "-client", flags)
                # The client currently maps an interrupted connection to exit 1.
                client.allowed_exit_codes = {"1"} if kind != "missing-client-key" else {"0"}
                sessions.append(client)
                if kind == "server-password":
                    prompt(client, "Server password required", "s3cretpass")
                    client.expect(lambda s: "Server password required" not in s and len(s.strip()) > 100, "connected", timeout=25)
                elif kind == "missing-client-key":
                    prompt(client, "REMOTE HOST IDENTIFICATION NOT KNOWN", "yes")
                    prompt(client, "CLIENT AUTHENTICATION REQUIRED", "n")
                    time.sleep(2)
                    assert (client.directory / "exit").exists()
                else:
                    prompt(client, "127.0.0.1", "yes")
                    client.expect(lambda s: "REMOTE HOST" not in s and "fingerprint" not in s and len(s.strip()) > 100, "accepted", timeout=25)
            results.append(dict(case=kind, status="pass"))
        except Exception as error:
            results.append(dict(case=kind, status="fail", error=str(error)))
        finally:
            for p in reversed(sessions):
                try:
                    p.close()
                except Exception as error:
                    results[-1].setdefault("shutdown_errors", []).append(p.name + ": " + str(error))
                    results[-1]["status"] = "fail"
        print(results[-1]["status"].upper(), kind, flush=True)
        (root / "auth-results.json").write_text(json.dumps(results, indent=2))
    raise SystemExit(any(r["status"] == "fail" for r in results))


if __name__ == "__main__":
    main()
