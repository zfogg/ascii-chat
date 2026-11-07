"""Test processes must not export telemetry, even when explicitly configured."""
from itertools import product
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import time

import pytest
from ascii_chat_tests.process import free_port

pytestmark = [pytest.mark.integration, pytest.mark.network]


def test_telemetry_disabled(binary, monkeypatch):
    root = Path(__file__).resolve().parents[4]
    monkeypatch.syspath_prepend(str(root / "deps/ascii-chat-deps/gotel/c/testing/python"))
    from gotel_testing import Collector
    binary = str(binary)
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
        env = dict(os.environ, TESTING="1", ASCII_CHAT_OTEL_ENABLED_TELEMETRY="traces,logs,metrics",
                   ASCII_CHAT_OTEL_ENDPOINT=receiver.url)
        result = subprocess.run([binary, "mcp-server", "--otel-endpoint", receiver.url,
                                 "--otel-enabled-telemetry", "traces,logs,metrics"],
                                input=b'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n',
                                env=env, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors="replace")[-3000:]
        assert b'"result"' in result.stdout
        assert not receiver.records, "MCP test sent OTLP to the collector"
        port = free_port()
        process = subprocess.Popen([binary, "--otel-endpoint", receiver.url, "--otel-enabled-telemetry", "traces,logs,metrics",
                                    "mcp-server", "--transport", "http", "--address", "127.0.0.1", "--port", str(port), "--status-screen=false"],
                                   env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    assert process.poll() is None, "MCP HTTP exited before listening"
                    time.sleep(.05)
            else:
                raise AssertionError("MCP HTTP did not start")
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=10)
            connection.request("POST", "/mcp", json.dumps({"jsonrpc": "2.0", "id": 1, "method": "tools/list"}),
                               {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"})
            response = connection.getresponse()
            assert response.status == 200
            assert len(json.loads(response.read())["result"]["tools"]) == 5
            connection.close()
        finally:
            process.terminate()
            process.communicate(timeout=10)
        assert not receiver.records, "MCP HTTP test sent OTLP to the collector"
    print("test-mode OTLP prohibition: 6 media endpoint/signal overrides plus MCP stdio/HTTP, zero exported requests")

