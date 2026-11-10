"""HTTP API mode smoke tests."""

import json
import subprocess
import socket
import time
import urllib.error
import urllib.request
from pathlib import Path


def _wait_for_health(url: str) -> dict:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        try:
            with urllib.request.urlopen(url, timeout=1) as response:
                return json.loads(response.read())
        except (OSError, urllib.error.URLError):
            time.sleep(0.1)
    raise AssertionError("api-server did not become healthy")


def test_api_server_health(binary: Path):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    process = subprocess.Popen(
        [str(binary), "api-server", "--address=127.0.0.1", f"--port={port}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        health = _wait_for_health(f"http://127.0.0.1:{port}/health")
        assert health["status"] == "ok"
        assert health["service"] == "ascii-chat-api"
        try:
            urllib.request.urlopen(f"http://127.0.0.1:{port}/convert", timeout=2)
        except urllib.error.HTTPError as error:
            assert error.code == 405
        else:
            raise AssertionError("GET /convert should return 405")
        request = urllib.request.Request(f"http://127.0.0.1:{port}/convert", data=b"not-an-image", method="POST",
                                         headers={"Content-Type": "text/plain"})
        try:
            urllib.request.urlopen(request, timeout=2)
        except urllib.error.HTTPError as error:
            assert error.code == 415
        else:
            raise AssertionError("text/plain should return 415")
        invalid = urllib.request.Request(
            f"http://127.0.0.1:{port}/convert",
            data=b"not-an-image",
            method="POST",
            headers={"Content-Type": "image/png"},
        )
        try:
            urllib.request.urlopen(invalid, timeout=2)
        except urllib.error.HTTPError as error:
            assert error.code == 422
        else:
            raise AssertionError("undecodable image should return 422")
        image = Path(__file__).parents[4] / "docs" / "images" / "audio-visualizations" / "frequencies.png"
        request = urllib.request.Request(
            f"http://127.0.0.1:{port}/convert?width=40&height=20&format=text&palette-chars=.-+%23",
            data=image.read_bytes(),
            method="POST",
            headers={"Content-Type": "image/png"},
        )
        with urllib.request.urlopen(request, timeout=10) as response:
            rendered = response.read()
            assert response.status == 200
            assert response.headers["Content-Type"].startswith("text/plain")
            assert rendered
        files_request = urllib.request.Request(
            f"http://127.0.0.1:{port}/convert?width=40&height=20",
            data=image.read_bytes(),
            method="POST",
            headers={"Content-Type": "image/png"},
        )
        with urllib.request.urlopen(files_request, timeout=10) as response:
            multipart_result = response.read()
            assert response.status == 200
            assert response.headers["Content-Type"].startswith("multipart/mixed")
            assert b"Content-Disposition: attachment; filename=frame-0.txt" in multipart_result
            assert b"--ascii-chat-frame--" in multipart_result
        boundary = b"ascii-chat-test-boundary"
        multipart = (
            b"--" + boundary + b"\r\n"
            b'Content-Disposition: form-data; name="image"; filename="image.png"\r\n'
            b"Content-Type: image/png\r\n\r\n"
            + image.read_bytes()
            + b"\r\n--"
            + boundary
            + b"--\r\n"
        )
        multipart_request = urllib.request.Request(
            f"http://127.0.0.1:{port}/convert?width=40&height=20&format=json&palette-chars=.-%2B%23",
            data=multipart,
            method="POST",
            headers={"Content-Type": f"multipart/form-data; boundary={boundary.decode()}"},
        )
        with urllib.request.urlopen(multipart_request, timeout=10) as response:
            assert response.status == 200
            assert json.loads(response.read())["ascii"]
    finally:
        process.terminate()
        process.wait(timeout=5)
