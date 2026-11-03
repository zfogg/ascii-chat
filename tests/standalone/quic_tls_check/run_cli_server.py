"""Exercise native QUIC participants against the real server process."""

from pathlib import Path
import os
import re
import socket
import subprocess
import sys
import tempfile
import time


def free_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


with tempfile.TemporaryDirectory(prefix="ascii-chat-quic-cli-") as temporary:
    root = Path(temporary)
    certificate, key, other_cert, other_key = (
        root / name for name in ("server.pem", "server.key", "other.pem", "other.key")
    )
    binary, fixture = sys.argv[1:3]
    subprocess.run([fixture, str(certificate), str(key), str(other_cert), str(other_key)], check=True, timeout=45)
    tcp_port = free_port(socket.SOCK_STREAM)
    websocket_port = free_port(socket.SOCK_STREAM)
    while websocket_port == tcp_port:
        websocket_port = free_port(socket.SOCK_STREAM)
    udp_port = free_port(socket.SOCK_DGRAM)
    command = [binary, "server", "127.0.0.1", "--port", str(tcp_port),
               "--quic-bind-address", "127.0.0.1", "--quic-port", str(udp_port),
               "--quic-tls-cert", str(certificate), "--quic-tls-key", str(key),
               "--quic-audio-datagrams",
               "--websocket-port", str(websocket_port), "--password", "test-password", "--status-screen=false"]
    flags = subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
    incomplete = subprocess.run([binary, "server", "127.0.0.1", "--quic-bind-address", "127.0.0.1",
                                 "--status-screen=false"], timeout=15, creationflags=flags, capture_output=True,
                                text=True, errors="replace", check=False)
    if incomplete.returncode == 0 or "Failed to join thread" in incomplete.stdout + incomplete.stderr:
        raise RuntimeError("Incomplete QUIC configuration did not fail cleanly")
    with (root / "server.log").open("wb") as log:
        server = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, creationflags=flags)
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    break
                if "Server accepting connections" in (root / "server.log").read_text(errors="replace"):
                    break
                time.sleep(0.05)
            else:
                raise RuntimeError("Server did not become ready")
            if server.poll() is not None:
                raise RuntimeError((root / "server.log").read_text(errors="replace"))
            probe_env = os.environ.copy()
            probe_env["ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK"] = "1"
            result = subprocess.run([fixture, "--cli-probe", str(certificate), str(udp_port)], timeout=30,
                                    creationflags=flags, check=False, capture_output=True, text=True, errors="replace",
                                    env=probe_env)
            if result.returncode:
                log.flush()
                sys.stderr.write(f"QUIC probe exited {result.returncode}\n{result.stdout}\n{result.stderr}\n")
                sys.stderr.write((root / "server.log").read_text(errors="replace"))
                raise SystemExit(result.returncode)
            client_log = root / "client.log"
            client = subprocess.run(
                [binary, "--log-file", str(client_log), "client", f"quic://localhost:{udp_port}", "--quic-ca-file", str(certificate),
                 "--password", "test-password", "--test-pattern", "--audio=false", "--snapshot",
                 "--snapshot-delay", "0", "--splash-screen=false", "--color-mode", "none",
                 "--reconnect-attempts", "0"],
                timeout=30, creationflags=flags, check=False, capture_output=True, text=True, errors="replace",
                env=probe_env,
            )
            client_logs = client_log.read_text(errors="replace") if client_log.exists() else ""
            if client.returncode or "using native QUIC" not in client_logs or not client.stdout.strip():
                log.flush()
                sys.stderr.write(f"QUIC client exited {client.returncode}\n{client.stdout}\n{client.stderr}\n{client_logs}\n")
                sys.stderr.write((root / "server.log").read_text(errors="replace"))
                raise SystemExit(client.returncode or 1)
            # Both clients stay connected long enough to send test-pattern video
            # and receive server-rendered frames while the other is present.
            server_log_offset = (root / "server.log").stat().st_size
            participants = []
            try:
                for index in range(2):
                    participant_log = root / f"participant-{index}.log"
                    output = (root / f"participant-{index}.out").open("wb")
                    error = (root / f"participant-{index}.err").open("wb")
                    process = subprocess.Popen(
                        [binary, "--log-file", str(participant_log), "client", f"quic://localhost:{udp_port}",
                         "--quic-ca-file", str(certificate), "--password", "test-password", "--test-pattern",
                         "--audio=false", "--quic-audio-datagrams", "--video-codec", "raw", "--snapshot", "--snapshot-delay", "8", "--splash-screen=false",
                         "--color-mode", "none", "--reconnect-attempts", "0"],
                        stdout=output, stderr=error, creationflags=flags, env=probe_env,
                    )
                    participants.append((process, participant_log, output, error))
                deadline = time.monotonic() + 35
                while time.monotonic() < deadline:
                    with (root / "server.log").open("rb") as server_output:
                        server_output.seek(server_log_offset)
                        session_logs = server_output.read().decode(errors="replace")
                    quic_clients = set(re.findall(r"Added new transport client (\S+).*type=5", session_logs))
                    video_clients = set(re.findall(r"Server image ingress client=(\S+)", session_logs))
                    active_video_clients = quic_clients & video_clients
                    if len(active_video_clients) >= 2 and all(p.poll() is None for p, _, _, _ in participants):
                        break
                    if any(p.poll() is not None for p, _, _, _ in participants):
                        raise RuntimeError(f"A QUIC participant exited before both sent video:\n{session_logs}")
                    time.sleep(0.05)
                else:
                    raise RuntimeError(f"Both QUIC participants did not send video:\n{session_logs}")
                for process, _, _, _ in participants:
                    process.wait(timeout=max(0.1, deadline - time.monotonic()))
                close_deadline = time.monotonic() + 8
                while time.monotonic() < close_deadline:
                    with (root / "server.log").open("rb") as server_output:
                        server_output.seek(server_log_offset)
                        session_logs = server_output.read().decode(errors="replace")
                    removed = set(re.findall(r"Removing client (\S+) - marking inactive", session_logs))
                    if active_video_clients <= removed:
                        break
                    time.sleep(0.05)
                else:
                    client_logs = "\n".join(log.read_text(errors="replace")[-12000:] for _, log, _, _ in participants)
                    raise RuntimeError(f"Server did not promptly remove QUIC participants:\n{client_logs}\n"
                                       f"{session_logs[-12000:]}")
                for index, (process, participant_log, output, error) in enumerate(participants):
                    output.close()
                    error.close()
                    participant_logs = participant_log.read_text(errors="replace") if participant_log.exists() else ""
                    if (process.returncode or "using native QUIC" not in participant_logs or
                            "CLIENT_CONNECTION_RUNNING" not in participant_logs or
                            not (root / f"participant-{index}.out").read_bytes().strip()):
                        raise RuntimeError(f"QUIC participant {index} failed: {participant_logs}\n"
                                           f"{(root / f'participant-{index}.err').read_text(errors='replace')}")
            finally:
                for process, _, output, error in participants:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=10)
                    output.close()
                    error.close()
            rejected_log = root / "rejected-client.log"
            rejected = subprocess.run(
                [binary, "--log-file", str(rejected_log), "client", f"quic://localhost:{udp_port}",
                 "--quic-ca-file", str(other_cert), "--password", "test-password", "--test-pattern",
                 "--audio=false", "--snapshot", "--snapshot-delay", "0", "--splash-screen=false",
                 "--reconnect-attempts", "0"],
                timeout=30, creationflags=flags, check=False, capture_output=True, text=True, errors="replace",
                env=probe_env,
            )
            rejected_logs = rejected_log.read_text(errors="replace") if rejected_log.exists() else ""
            if (rejected.returncode == 0 or "using native QUIC" in rejected_logs or
                    "QUIC connection establishment failed: Cryptographic error" not in rejected_logs):
                raise RuntimeError(f"Untrusted QUIC certificate was not rejected: {rejected_logs}")
        finally:
            if server.poll() is None:
                server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=10)
