"""Exercise native rendered-frame rejection telemetry without a camera.
Run: python tests/manual/test_otel_frame_validation.py build/bin/asciichat.dll
Optional FRAME_VALIDATION_REPLAY_ENDPOINT forwards captured payloads to Spaniel.
"""
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "deps/ascii-chat-deps/gotel/c-gotel/testing/python"))
from gotel_testing import Collector, records, protobuf


def child(library, endpoint):
    os.environ["ASCII_CHAT_OTEL_ENDPOINT"] = endpoint
    os.environ["ASCII_CHAT_OTEL_REALTIME_SAMPLE_RATE"] = "0"
    dll = ctypes.CDLL(str(Path(library).resolve()))
    assert dll.asciichat_shared_init(None, False, True) == 0
    argv = (ctypes.c_char_p * 3)(b"ascii-chat", b"mirror", None)
    assert dll.options_init(2, argv) == 0
    assert dll.ascii_otel_init(b"mirror", 2, argv) == 0
    validate = dll.session_frame_validate
    validate.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
    assert validate(b"AB\nCD\n", 6, 2, 2) == 0
    # Failed media operations are deliberately sampled out; rejection reports must survive.
    scope = (ctypes.c_uint64 * 128)()
    dll.ascii_otel_scope_begin(scope, 0)
    for data, width, height in [(b"ABC\nCD\n", 2, 2), (b"\xff", 1, 1),
                                (b"\x1b]2;bad\x07A\n", 1, 1), (b"\x1b[31mA\n", 1, 1)]:
        assert validate(data, len(data), width, height) != 0
        dll.errno_clear()
    dll.ascii_otel_scope_end(scope)
    dll.ascii_otel_shutdown()


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "--child":
        child(sys.argv[2], sys.argv[3])
        return
    with Collector() as receiver:
        result = subprocess.run([sys.executable, __file__, "--child", sys.argv[1], receiver.url],
                                capture_output=True, timeout=30)
        assert result.returncode == 0, result.stderr.decode(errors="replace")[-4000:]
        spans = [span for span in records(receiver.records, "/v1/traces")
                 if span.get(5) == [b"ascii-chat.frame.validation.rejected"]]
        assert len(spans) == 4, len(spans)
        for span in spans:
            assert protobuf(span[15][0])[3] == [2]
            attributes = {protobuf(a)[1][0].decode(): protobuf(protobuf(a)[2][0]) for a in span.get(9, [])}
            assert attributes["error.type"][1] == [b"ascii_chat.invalid_frame"]
            assert attributes["frame.validation.reason"][1][0]
            assert attributes["asciichat_errno.code"][3][0] > 0
            for key in ("frame.width", "frame.height", "frame.size_bytes"):
                assert attributes[key][3][0] > 0
        for name, expected in [(b"ascii-chat.frame.validation.checked", 5),
                               (b"ascii-chat.frame.validation.rejected", 4)]:
            metrics = [m for m in records(receiver.records, "/v1/metrics") if m.get(1) == [name]]
            assert metrics, name
            values = [struct.unpack("<d", protobuf(point)[4][0])[0] for metric in metrics
                      for point in protobuf(metric[7][0]).get(1, [])]
            assert max(values) == expected, (name, values)
        print(json.dumps({"valid_frames": 1, "rejected_frames": 4,
                          "trace_ids": [s[1][0].hex() for s in spans]}))
        endpoint = os.environ.get("FRAME_VALIDATION_REPLAY_ENDPOINT")
        if endpoint:
            for route in ("/v1/traces", "/v1/metrics", "/v1/logs"):
                payload = b"".join(body for path, body in receiver.records if path == route)
                if payload:
                    urllib.request.urlopen(urllib.request.Request(endpoint + route, data=payload,
                        headers={"Content-Type": "application/x-protobuf"}), timeout=15).close()

if __name__ == "__main__":
    main()
