"""Exercise OpenSSL download retries and archive validation without external network access."""

import hashlib
import http.server
import shutil
import subprocess
import threading
from pathlib import Path

import pytest

HELPER = Path(__file__).resolve().parents[3] / "cmake/utils/Retry.cmake"
PAYLOAD = b"verified OpenSSL archive fixture"


@pytest.mark.parametrize("failures", [2, 4])
def test_openssl_download_retries(tmp_path, failures):
    if not shutil.which("cmake"):
        pytest.skip("cmake is required")

    class Handler(http.server.BaseHTTPRequestHandler):
        attempts = 0

        def do_GET(self):
            type(self).attempts += 1
            if self.attempts <= failures:
                self.send_response(503)
                self.end_headers()
                return
            self.send_response(200)
            self.end_headers()
            self.wfile.write(PAYLOAD)

        def log_message(self, format, *args):
            pass

    server = http.server.HTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    destination = tmp_path / "openssl.tar.gz"
    destination.write_bytes(b"invalid cached archive")
    script = tmp_path / "download.cmake"
    digest = hashlib.sha256(PAYLOAD).hexdigest()
    script.write_text(
        f'include("{HELPER.as_posix()}")\n'
        f'asciichat_download_with_retry("http://127.0.0.1:{server.server_port}/archive" '
        f'"{destination.as_posix()}" "{digest}")\n',
        encoding="utf-8",
    )
    try:
        result = subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True, check=False)
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
    assert Handler.attempts == min(failures + 1, 4)
    assert not destination.with_suffix(".gz.download").exists()
    if failures < 4:
        assert result.returncode == 0, result.stderr
        assert destination.read_bytes() == PAYLOAD
        assert "retrying in 1s" in result.stdout and "retrying in 2s" in result.stdout
    else:
        assert result.returncode != 0
        assert "after 4 attempts" in result.stderr
        assert not destination.exists()

