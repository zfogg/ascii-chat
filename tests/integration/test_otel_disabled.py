"""Prove test processes never export, even with an explicit collector and signals."""
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment
from itertools import product
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "deps/ascii-chat-deps/gotel/c/testing/python"))
from gotel_testing import Collector

def main():
    binary = str(Path(sys.argv[1]).resolve())
    # A listening loopback receiver proves the prohibition; no OTLP is sent to it.
    with Collector() as receiver:
        for flag, protocol in product(("1", "true", "0"), ("http/protobuf", "grpc")):
            env = dict(os.environ, TESTING=flag, ASCII_CHAT_OTEL_ENABLED_TELEMETRY="traces,logs,metrics",
                       OTEL_EXPORTER_OTLP_ENDPOINT=receiver.url, ASCII_CHAT_OTEL_ENDPOINT=receiver.url,
                       OTEL_EXPORTER_OTLP_PROTOCOL=protocol)
            result = subprocess.run([binary, "mirror", "--test-pattern", "--snapshot", "--snapshot-delay", "0",
                                     "--audio=false", "--width", "20", "--height", "10",
                                     "--otel-endpoint", receiver.url, "--otel-enabled-telemetry", "traces,logs,metrics"],
                                    env=env, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors="replace")[-3000:]
            assert not receiver.records, "test sent OTLP to the collector"
            assert b"Telemetry enabled" not in result.stderr
    print("test-mode OTLP prohibition: 6 HTTP/gRPC endpoint/signal overrides, zero exported requests")

if __name__ == "__main__":
    main()
