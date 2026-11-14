"""Mutual SSH identity and rejected peers over TCP and WebSocket."""
import base64
import shutil
import subprocess

import pytest
from ascii_chat_tests.cli import clean_environment, run_cli
from ascii_chat_tests.process import Service, free_port, server_options

pytestmark = [pytest.mark.integration, pytest.mark.network]


@pytest.mark.parametrize("with_password", [False, True])
@pytest.mark.parametrize("transport", ["tcp", "websocket"])
def test_identity_sessions(binary, tmp_path, with_password, transport):
    root = tmp_path
    if not shutil.which("ssh-keygen"):
        pytest.skip("ssh-keygen is required for identity sessions")
    for name in ("server", "client", "outsider"):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "identity-fixture-" + name,
                        "-f", str(root / name)], check=True, timeout=15)
    public_wire = base64.b64decode((root / "server.pub").read_text().split()[1])
    assert len(public_wire) == 51 and public_wire[4:15] == b"ssh-ed25519"
    configuration = root / "ascii-chat"
    configuration.mkdir(exist_ok=True)
    port, ws_port = free_port(), free_port()
    while ws_port == port:
        ws_port = free_port()
    (configuration / "known_hosts").write_text(
        "".join(f"127.0.0.1:{p} x25519 {public_wire[-32:].hex()} ascii-chat\n" for p in (port, ws_port)))
    password = ["--password", "identity-session-fixture"] if with_password else []
    server = Service(binary, root, "identity-server-" + str(with_password), "server",
                     server_options(port, ws_port) + ["--key", str(root / "server"), "--client-keys",
                     str(root / "client.pub"), *password])
    try:
        server.wait_for("Server accepting connections")
        for destination in ([f"127.0.0.1:{port}"] if transport == "tcp" else [f"ws://127.0.0.1:{ws_port}"]):
            for key, expected_server, succeeds in (("outsider", "server", False),
                                                    ("client", "outsider", False),
                                                    ("client", "server", True)):
                before = server.text().count("Client key authentication successful (whitelist verified)")
                before_pin = server.text().count("Unknown pinned server identity")
                before_rejected = server.text().count("Client Ed25519 key not in whitelist")
                log = root / "identity-client.log"
                env = clean_environment()
                env.pop("CLAUDECODE", None)
                env.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root))
                result = run_cli(str(binary), "--log-level", "debug",
                    "--log-file", str(log), "client", destination, "--key", str(root / key),
                    "--server-key", str(root / (expected_server + ".pub")), *password,
                    "--test-pattern", "--audio=false", "--video-codec", "raw", "--snapshot",
                    "--snapshot-delay", "0", "--width", "40", "--height", "12", "--color-mode", "none", "--strip-ansi",
                    "--splash-screen=false", env=env, cwd=root, capture_output=True, timeout=25)
                text = log.read_text(errors="replace")
                assert server.process.poll() is None, server.text()[-5000:]
                if succeeds:
                    assert result.returncode == 0, (destination, result.returncode, text[-5000:])
                    assert len(result.stdout.strip()) >= 400, result.stdout
                    assert server.text().count("Client key authentication successful (whitelist verified)") > before
                    assert "Connected" in text
                    assert "Server key matches known_hosts" in text
                else:
                    assert result.returncode != 0, (destination, key, expected_server, text[-5000:])
                    assert not result.stdout.strip(), result.stdout
                    if key == "outsider":
                        assert server.text().count("Client Ed25519 key not in whitelist") > before_rejected, \
                            (server.text()[-5000:], text[-5000:])
                    else:
                        assert server.text().count("Unknown pinned server identity") > before_pin, server.text()[-5000:]
            print(f"PASS: {destination}: rejects unlisted client/wrong server and renders for authorized identities; "
                  f"password={with_password}", flush=True)
    finally:
        server.stop()
