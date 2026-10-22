"""Native synthetic media integration against a real OTLP/HTTP receiver."""
import http.server
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import tempfile
import json
import socket
import struct

def protobuf(data):
    fields, offset = {}, 0
    def varint():
        nonlocal offset
        value = shift = 0
        while True:
            byte = data[offset]
            offset += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
            shift += 7
            assert shift < 70
    while offset < len(data):
        tag = varint()
        key, wire = tag >> 3, tag & 7
        if wire == 0:
            value = varint()
        else:
            size = varint() if wire == 2 else {1: 8, 5: 4}[wire]
            value = data[offset:offset + size]
            assert len(value) == size
            offset += size
        fields.setdefault(key, []).append(value)
    return fields

def signal_records(requests, signal):
    output = []
    for path, payload in requests:
        if path != '/v1/' + signal:
            continue
        for resource in protobuf(payload).get(1, []):
            for scope in protobuf(resource).get(2, []):
                output.extend(protobuf(record) for record in protobuf(scope).get(2, []))
    return output

def service_names(requests):
    names = set()
    for _, payload in requests:
        for resource in protobuf(payload).get(1, []):
            attributes = protobuf(protobuf(resource)[1][0]).get(1, [])
            for attribute in attributes:
                entry = protobuf(attribute)
                if entry[1] == [b'service.name']:
                    names.add(protobuf(entry[2][0])[1][0].decode())
    return names

class Receiver(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.server.records.append((self.path, body))
        self.send_response(200)
        self.send_header('Content-Type', 'application/x-protobuf')
        self.send_header('Content-Length', '0')
        self.end_headers()
    def log_message(self, *args):
        pass

def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def network_smoke(binary, receiver, env):
    receiver.records.clear()
    port = free_port()
    base = [binary, '--no-check-update', '--otel-endpoint',
            f'http://127.0.0.1:{receiver.server_port}', '--otel-realtime-sample-rate', '1',
            '--otel-realtime-max-per-second', '1000', '--otel-metrics-interval', '50']
    # Local ephemeral-DH fixture bypasses TOFU file mutation; encryption remains enabled.
    fixture_env = dict(env, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1')
    server = subprocess.Popen(base + ['server', '127.0.0.1', '--port', str(port), '--status-screen=false'],
                              env=fixture_env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 10
        while True:
            assert server.poll() is None, 'server exited before becoming ready'
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=.1):
                    break
            except OSError:
                assert time.monotonic() < deadline, 'server readiness deadline'
                time.sleep(.05)
        result = subprocess.run(base + ['client', f'127.0.0.1:{port}', '--test-pattern',
             '--audio=false', '--snapshot', '--snapshot-delay', '0', '--width', '40', '--height', '12',
             '--splash-screen=false'], env=fixture_env, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
        assert len(result.stdout) > 40
        traces = b''.join(body for path, body in receiver.records if path == '/v1/traces')
        assert b'crypto_handshake_client_key_exchange' in traces
        assert b'ascii-chat.server.video.render' in traces
        assert service_names(receiver.records) == {'ascii-chat-client', 'ascii-chat-server'}
    finally:
        server.terminate()
        server.wait(timeout=5)

def database_smoke(binary, receiver, env):
    receiver.records.clear()
    with tempfile.TemporaryDirectory() as temp:
        process = subprocess.Popen([binary, '--no-check-update', '--otel-endpoint',
            f'http://127.0.0.1:{receiver.server_port}', 'discovery-service', '127.0.0.1',
            '--port', str(free_port()), '--database', str(Path(temp) / 'discovery.db'),
            '--status-screen=false'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while True:
                assert process.poll() is None, 'discovery-service exited before exporting SQL'
                traces = b''.join(body for path, body in receiver.records if path == '/v1/traces')
                metrics = b''.join(body for path, body in receiver.records if path == '/v1/metrics')
                if b'ascii-chat.storage.EnableWAL' in traces and b'db.client.operation.duration' in metrics:
                    break
                assert time.monotonic() < deadline, 'named SQLite export deadline'
                time.sleep(.05)
            metrics = b''.join(body for path, body in receiver.records if path == '/v1/metrics')
            assert b'db.client.operation.duration' in metrics
            assert service_names(receiver.records) == {'ascii-chat-discovery-service'}
        finally:
            process.terminate()
            process.wait(timeout=5)

def main():
    binary = str(Path(sys.argv[1]).resolve())
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Receiver)
    server.records = []
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = [binary, '--no-check-update']
    media = ['mirror', '--test-pattern', '--audio=false', '--snapshot',
             '--snapshot-delay', '0', '--width', '40', '--height', '12', '--splash-screen=false']
    env = {k: v for k, v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    try:
        for signals, paths in [('traces,logs,metrics', {'traces', 'logs', 'metrics'}),
                               ('traces', {'traces'}), ('metrics', {'metrics'}), ('none', set())]:
            server.records.clear()
            result = subprocess.run(base + ['--otel-endpoint', f'http://127.0.0.1:{server.server_port}',
                 '--otel-enabled-telemetry', signals, '--otel-realtime-sample-rate', '1',
                 '--otel-realtime-max-per-second', '1000'] + media,
                 env=env, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors='replace')[-6000:]
            assert {path.rsplit('/', 1)[-1] for path, _ in server.records} == paths
            assert service_names(server.records) == ({'ascii-chat-mirror'} if paths else set())
            traces = b''.join(b for p, b in server.records if p == '/v1/traces')
            metrics = b''.join(b for p, b in server.records if p == '/v1/metrics')
            if 'traces' in paths:
                for name in [b'ascii-chat.cli', b'ascii-chat.pipeline.capture',
                             b'ascii-chat.pipeline.display', b'session_display_write_ascii_frame']:
                    assert name in traces, name
                assert b'process.command_args' in traces
                assert b'process.exit.code' in traces
                assert b'http://127.0.0.1:' not in traces, 'CLI argument values were not redacted'
                spans = signal_records(server.records, 'traces')
                capture = next(s for s in spans if s[5] == [b'ascii-chat.pipeline.capture'])
                display = next(s for s in spans if s[5] == [b'ascii-chat.pipeline.display'])
                assert capture[1] == display[1], 'frame context was lost across worker queues'
                assert display[4] == capture[2], 'display span did not retain capture parent'
            if 'metrics' in paths:
                assert b'ascii-chat.pipeline.capture.count' in metrics
                assert b'duration.sum' in metrics
                for metric in signal_records(server.records, 'metrics'):
                    if 7 in metric:
                        point = protobuf(protobuf(metric[7][0])[1][0])
                        start = struct.unpack('<Q', point[2][0])[0]
                        end = struct.unpack('<Q', point[3][0])[0]
                        assert start <= end and abs(end - time.time_ns()) < 60_000_000_000, (start, end)
            assert len(result.stdout) > 40, 'Synthetic snapshot produced no frame'
        # Sampling off suppresses hot spans but leaves their metrics intact.
        server.records.clear()
        result = subprocess.run(base + ['--otel-endpoint', f'http://127.0.0.1:{server.server_port}',
             '--otel-realtime-sample-rate', '0'] + media, env=env, capture_output=True, timeout=20)
        assert result.returncode == 0
        traces = b''.join(b for p, b in server.records if p == '/v1/traces')
        metrics = b''.join(b for p, b in server.records if p == '/v1/metrics')
        assert b'ascii-chat.pipeline.capture' not in traces
        assert b'ascii-chat.pipeline.capture.count' in metrics
        started = time.monotonic()
        result = subprocess.run(base + ['--otel-endpoint', 'http://127.0.0.1:1',
             '--otel-timeout', '100'] + media, env=env, capture_output=True, timeout=10)
        assert result.returncode == 0
        assert time.monotonic() - started < 8
        for flag, value in [('--otel-queue-capacity', '3'), ('--otel-enabled-telemetry', 'invalid'),
                            ('--otel-endpoint', 'https://collector.example'), ('--otel-log-level', 'invalid')]:
            args = base + ['--otel-endpoint', f'http://127.0.0.1:{server.server_port}', flag, value] + media
            result = subprocess.run(args, env=env, capture_output=True, timeout=10)
            assert result.returncode != 0, (flag, value)
        # TOML config, environment and CLI must resolve in increasing precedence.
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / 'otel.toml'
            config.write_text('[otel]\nendpoint = "http://127.0.0.1:%d"\nenabled_telemetry = "metrics"\nrealtime_sample_rate = 0.0\n' % server.server_port)
            server.records.clear()
            result = subprocess.run(base + ['--config', str(config)] + media,
                                    env=env, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
            assert {p for p, _ in server.records} == {'/v1/metrics'}, [p for p, _ in server.records]
            server.records.clear()
            override_env = dict(env, ASCII_CHAT_OTEL_ENABLED_TELEMETRY='logs')
            result = subprocess.run(base + ['--config', str(config)] + media,
                                    env=override_env, capture_output=True, timeout=20)
            assert result.returncode == 0
            assert {p for p, _ in server.records} == {'/v1/logs'}, [p for p, _ in server.records]
            server.records.clear()
            result = subprocess.run(base + ['--config', str(config), '--otel-enabled-telemetry', 'traces'] + media,
                                    env=override_env, capture_output=True, timeout=20)
            assert result.returncode == 0
            assert {p for p, _ in server.records} == {'/v1/traces'}, [p for p, _ in server.records]
        network_smoke(binary, server, env)
        database_smoke(binary, server, env)
        print('ascii-chat OTLP integration: signals, media, sampling, offline collector, config precedence, encrypted networking and SQLite passed')
    finally:
        server.shutdown()
        server.server_close()

if __name__ == '__main__':
    main()
