"""Native synthetic media integration against a real OTLP/HTTP receiver."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import os
from pathlib import Path
import subprocess
import sys
import time
import tempfile
import json
import socket
import struct
import socketserver
import threading

sys.path.insert(0, str(Path(__file__).resolve().parents[2] /
    'deps/ascii-chat-deps/gotel/c/testing/python'))
from gotel_testing import Collector, protobuf, records, service_names


def signal_records(requests, signal):
    return records(requests, '/v1/' + signal)


def interrupted_collector_smoke(binary, env):
    """Exercise real sockets that crash or stall after consuming an HTTP request."""
    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            self.connection.settimeout(2)
            try:
                length = 0
                while line := self.rfile.readline():
                    if line == b'\r\n':
                        break
                    if line.lower().startswith(b'content-length:'):
                        length = int(line.split(b':', 1)[1])
                self.rfile.read(length)
                self.server.received.set()
                if self.server.mode == 'close':
                    return
                partial = (b'HTTP/1.1 200 OK\r\nContent-Length: ' if self.server.mode == 'headers'
                           else b'HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\nx')
                self.wfile.write(partial)
                self.wfile.flush()
                self.server.stop.wait(12)
            except (OSError, ValueError):
                pass

    class Server(socketserver.ThreadingTCPServer):
        daemon_threads = True
        block_on_close = False

    for mode in ('close', 'headers', 'body'):
        with Server(('127.0.0.1', 0), Handler) as receiver, tempfile.TemporaryDirectory() as temp:
            receiver.mode = mode
            receiver.stop = threading.Event()
            receiver.received = threading.Event()
            thread = threading.Thread(target=receiver.serve_forever, kwargs={'poll_interval': .01}, daemon=True)
            thread.start()
            started = time.monotonic()
            try:
                log = Path(temp) / 'collector.log'
                # The stalled body also exercises the production timeout default.
                timeout_flags = [] if mode == 'body' else ['--otel-timeout', '100']
                duration = '6' if mode == 'body' else '3'
                result = subprocess.run([binary, '--no-check-update', '--log-file', str(log), '--otel-endpoint',
                    f'http://127.0.0.1:{receiver.server_address[1]}'] + timeout_flags + [
                    'mirror', '--test-pattern', '--audio=false', '--snapshot', '--snapshot-delay', duration,
                    '--width', '40', '--height', '12', '--splash-screen=false'],
                    env=env, capture_output=True, timeout=24)
                assert receiver.received.is_set(), (mode, 'no HTTP request reached fault fixture')
                assert result.returncode == 0, (mode, result.stderr[-2000:])
                assert len(result.stdout) > 40, (mode, 'rendering stopped during collector failure')
                assert time.monotonic() - started < (22 if mode == 'body' else 10), (mode, 'shutdown deadline exceeded')
                messages = log.read_bytes()
                diagnostic = b'Telemetry collector export failed' in messages
                if mode == 'body':
                    diagnostic |= b'Telemetry worker did not stop within its deadline' in messages
                assert diagnostic, (mode, messages[-2000:])
                assert messages.count(b'Telemetry collector export failed') <= 1, 'failure log spam'
            finally:
                receiver.stop.set()
                receiver.shutdown()
                thread.join(timeout=3)


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def network_smoke(binary, receiver, env):
    receiver.clear()
    port = free_port()
    base = [binary, '--no-check-update', '--otel-endpoint',
            f'http://127.0.0.1:{receiver.server_port}', '--otel-realtime-sample-rate', '1',
            '--otel-realtime-max-per-second', '1000', '--otel-metrics-interval', '50']
    # Local ephemeral-DH fixture bypasses TOFU file mutation; encryption remains enabled.
    fixture_env = dict(env, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1')
    server = subprocess.Popen(base + ['server', '127.0.0.1', '--port', str(port), '--status-screen=false', '--websocket-port', str(free_port())],
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
             '--audio=false', '--snapshot', '--snapshot-delay', '2', '--width', '120', '--height', '40',
             '--color-mode', 'truecolor',
             '--splash-screen=false'], env=fixture_env, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
        assert len(result.stdout) > 40
        traces = b''.join(body for path, body in receiver.records if path == '/v1/traces')
        assert b'crypto_handshake_client_key_exchange' in traces
        assert b'ascii-chat.server.video.render' in traces
        spans = signal_records(receiver.records, 'traces')
        by_id = {s[2][0]: s for s in spans}
        packets = [s for s in spans if b'network.packet.name' in b''.join(s.get(9, []))]
        assert packets and any(b'.ASCII_FRAME' in s[5][0] for s in packets), 'packet names missing from spans'
        assert all(b'network.protocol.name' in b''.join(s.get(9, [])) for s in packets)
        crypto = [s for s in spans if s[5] == [b'ascii-chat.crypto.packet.encrypt']]
        assert crypto and any(b'crypto.output.size' in b''.join(s.get(9, [])) for s in crypto), 'core crypto metadata missing'
        compression = [s for s in spans if s[5] == [b'ascii-chat.compression.zstd.compress_workspace']]
        assert compression and any(b'compression.ratio.permille' in b''.join(s.get(9, [])) for s in compression)
        assert any(c[1] == e[1] for c in compression for e in crypto), 'compression and encryption lost frame context'
        def ancestor(span, name):
            seen = set()
            while span.get(4) and span[4][0] in by_id and span[4][0] not in seen:
                seen.add(span[4][0])
                parent = by_id[span[4][0]]
                assert parent[1] == span[1], 'parent and child have different trace IDs'
                if parent[5] == [name] or parent[5][0].startswith(name + b'.'):
                    return True
                span = parent
            return False
        assert any(s[5] == [b'ascii-chat.video.encode'] and
                   ancestor(s, b'ascii-chat.client.video.frame') for s in spans), 'encoding lost frame parent'
        assert any(s[5] == [b'ascii-chat.display.session_display_write_ascii_frame'] and
                   ancestor(s, b'ascii-chat.network.packet.dispatch') for s in spans), 'receive processing is detached'
        assert any(s[5][0].startswith(b'ascii-chat.network.outbound.send.') and
                   ancestor(s, b'ascii-chat.client.video.frame') for s in spans), 'outbound worker lost producer context'
        # Builds without a software encoder negotiate raw RGB instead.
        if any(s[5] == [b'ascii-chat.ffmpeg.video.decode'] for s in spans):
            assert any(s[5] == [b'ascii-chat.ffmpeg.decoder.receive_frame'] and
                       ancestor(s, b'ascii-chat.network.packet.dispatch') for s in spans), 'server decode lost receive parent'
        metrics = b''.join(body for path, body in receiver.records if path == '/v1/metrics')
        for name in [b'ascii-chat.network.sent', b'ascii-chat.network.received',
                     b'ascii-chat.network.packet.sent_size', b'ascii-chat.network.packet.received_size',
                     b'ascii-chat.network.round_trip_time', b'ascii-chat.network.outbound.queue.wait',
                     b'ascii-chat.network.connections.established']:
            assert name in metrics, name
        assert service_names(receiver.records) == {'ascii-chat-client', 'ascii-chat-server'}
    finally:
        server.terminate()
        server.wait(timeout=5)

def database_smoke(binary, receiver, env):
    receiver.clear()
    with tempfile.TemporaryDirectory() as temp:
        process = subprocess.Popen([binary, '--no-check-update', '--otel-endpoint',
            f'http://127.0.0.1:{receiver.server_port}', '--otel-metrics-interval', '50', 'discovery-service', '127.0.0.1',
            '--port', str(free_port()), '--websocket-port', str(free_port()), '--database', str(Path(temp) / 'discovery.db'),
            '--status-screen=false'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            def exported(requests):
                assert process.poll() is None, 'discovery-service exited before exporting SQL'
                traces = b''.join(body for path, body in requests if path == '/v1/traces')
                metrics = b''.join(body for path, body in requests if path == '/v1/metrics')
                return (all(name in traces for name in [b'ascii-chat.storage.EnableWAL',
                            b'ascii-chat.storage.SchemaSessions', b'ascii-chat.storage.SchemaIdxRateEvents',
                            b'ascii-chat.discovery.initialize', b'ascii-chat.discovery.database.initialize'])
                        and all(name in metrics for name in [b'db.client.operation.duration',
                            b'ascii-chat.discovery.sessions.active', b'ascii-chat.discovery.participants.active']))
            assert receiver.wait_for(exported, timeout=10), 'named SQLite export deadline'
            traces = b''.join(body for path, body in receiver.records if path == '/v1/traces')
            metrics = b''.join(body for path, body in receiver.records if path == '/v1/metrics')
            assert b'db.client.operation.duration' in metrics
            assert b'ascii-chat.discovery.sessions.active' in metrics
            assert b'ascii-chat.discovery.participants.active' in metrics
            assert service_names(receiver.records) == {'ascii-chat-discovery-service'}
            assert b'ascii-chat.storage.SchemaSessions' in traces
            assert b'ascii-chat.storage.SchemaIdxRateEvents' in traces
            assert b'ascii-chat.storage.UnknownQuery' not in traces
            spans = signal_records(receiver.records, 'traces')
            by_id = {s[2][0]: s for s in spans}
            initialization = next(s for s in spans if s[5] == [b'ascii-chat.discovery.initialize'])
            database = next(s for s in spans if s[5] == [b'ascii-chat.discovery.database.initialize'])
            assert not initialization.get(4) or not any(initialization[4][0]), 'startup inherited a pending lifetime span'
            assert database[4] == initialization[2]
            for span in spans:
                if span.get(4) and any(span[4][0]):
                    assert span[4][0] in by_id, ('missing parent while service is running', span[5])
                    assert span[1] == by_id[span[4][0]][1]
            schema = next(s for s in spans if s[5] == [b'ascii-chat.storage.SchemaSessions'])
            assert schema[4] == database[2], 'SQL must belong to database initialization'
        finally:
            process.terminate()
            process.wait(timeout=5)

def discovery_trace_smoke(binary, receiver):
    import discovery_webrtc

    receiver.clear()
    original_service = discovery_webrtc.Service

    class InstrumentedService(original_service):
        def __init__(self, executable, directory, name, mode, options, environment=None):
            environment = dict(environment or {}, ASCII_CHAT_OTEL_ENDPOINT=receiver.url,
                               ASCII_CHAT_OTEL_ENABLED_TELEMETRY='traces',
                               ASCII_CHAT_OTEL_REALTIME_SAMPLE_RATE='1',
                               ASCII_CHAT_OTEL_REALTIME_MAX_PER_SECOND='10000')
            super().__init__(executable, directory, name, mode, options, environment)

    discovery_webrtc.Service = InstrumentedService
    try:
        with tempfile.TemporaryDirectory(prefix='otel-discovery-chains-') as temp:
            discovery_webrtc.exercise(Path(binary), Path(temp))
        spans = signal_records(receiver.records, 'traces')
        renders = [span for span in spans if span[5] == [b'ascii-chat.server.video.render']]
        assert renders, 'no native discovery host render spans'
        assert any(any(child[1] == root[1] and child[5] == [b'ascii-chat.video.ascii_frame_init']
                       for child in spans) for root in renders), 'native host conversion lost frame context'
        for tick in (span for span in spans if span[5] == [b'ascii-chat.discovery.handoff.tick']):
            assert any(span[1] == tick[1] and span[2] != tick[2] for span in spans), 'empty handoff trace'
    finally:
        discovery_webrtc.Service = original_service


def main():
    print("SKIP: collector-export checks are prohibited in test mode; use in-process transport tests and test_otel_disabled.py")
    return 77

    binary = str(Path(sys.argv[1]).resolve())
    server = Collector()
    base = [binary, '--no-check-update']
    media = ['mirror', '--test-pattern', '--audio=false', '--snapshot',
             '--snapshot-delay', '0', '--width', '40', '--height', '12', '--splash-screen=false']
    env = {k: v for k, v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    try:
        for signals, paths in [('traces,logs,metrics', {'traces', 'logs', 'metrics'}),
                               ('traces', {'traces'}), ('metrics', {'metrics'}), ('none', set())]:
            server.clear()
            result = subprocess.run(base + ['--otel-endpoint', f'http://127.0.0.1:{server.server_port}',
                 '--otel-enabled-telemetry', signals, '--otel-realtime-sample-rate', '1',
                 '--otel-realtime-max-per-second', '1000'] + media,
                 env=env, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors='replace')[-6000:]
            assert {path.rsplit('/', 1)[-1] for path, _ in server.records} == paths
            assert service_names(server.records) == ({'ascii-chat-mirror'} if paths else set())
            for _, payload in server.records:
                for envelope in protobuf(payload).get(1, []):
                    resource = protobuf(protobuf(envelope)[1][0])
                    attributes = {}
                    for entry in resource.get(1, []):
                        entry = protobuf(entry)
                        attributes[entry[1][0]] = protobuf(entry[2][0])
                    for key in (b'process.pid', b'process.executable.path', b'process.executable.name',
                                b'host.name', b'os.type', b'os.description', b'process.command_args'):
                        assert key in attributes, ('missing resource', key)
                    args = [protobuf(value)[1][0] for value in protobuf(attributes[b'process.command_args'][5][0])[1]]
                    assert args[0] == b'ascii-chat'
                    assert not any(b'http://127.0.0.1:' in value for value in args), 'resource args bypassed redaction'
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
                initialization = next(s for s in spans if s[5] == [b'ascii-chat.session.initialize'])
                cleanup = next(s for s in spans if s[5] == [b'ascii-chat.session.cleanup'])
                display_setup = next(s for s in spans if s[5] == [b'ascii-chat.display.session_display_create'])
                media_cleanup = next(s for s in spans if s[5] == [b'ascii-chat.media.media_source_destroy'])
                assert initialization[1] == display_setup[1], 'display setup lost its lifecycle trace'
                assert cleanup[1] == media_cleanup[1], 'media cleanup lost its lifecycle trace'
                assert initialization[1] != cleanup[1], 'setup and shutdown must be bounded independently'
                capture = next(s for s in spans if s[5] == [b'ascii-chat.pipeline.capture'])
                display = next(s for s in spans if s[5] == [b'ascii-chat.pipeline.display'])
                assert capture[1] == display[1], 'frame context was lost across worker queues'
                assert display[4] == capture[2], 'display span did not retain capture parent'
            if 'metrics' in paths:
                assert b'ascii-chat.pipeline.capture.count' in metrics
                assert b'duration.sum' in metrics
                for name in [b'ascii-chat.frames.captured', b'ascii-chat.frames.displayed',
                             b'ascii-chat.frames.capture_to_display', b'ascii-chat.queue.display.wait',
                             b'ascii-chat.process.cpu.time', b'ascii-chat.process.memory.resident',
                             b'ascii-chat.process.threads', b'ascii-chat.otel.tick.duration']:
                    assert name in metrics, name
                histograms = [m for m in signal_records(server.records, 'metrics') if 9 in m]
                assert histograms, 'No latency histograms were exported'
                for histogram in histograms:
                    point = protobuf(protobuf(histogram[9][0])[1][0])
                    count = struct.unpack('<Q', point[4][0])[0]
                    buckets = struct.unpack('<' + 'Q' * (len(point[6][0]) // 8), point[6][0])
                    assert sum(buckets) == count and count > 0, histogram[1]
                for metric in signal_records(server.records, 'metrics'):
                    if metric[1] == [b'ascii-chat.process.memory.resident']:
                        point = protobuf(protobuf(metric[5][0])[1][0])
                        assert struct.unpack('<d', point[4][0])[0] > 0
                for metric in signal_records(server.records, 'metrics'):
                    if 7 in metric:
                        point = protobuf(protobuf(metric[7][0])[1][0])
                        start = struct.unpack('<Q', point[2][0])[0]
                        end = struct.unpack('<Q', point[3][0])[0]
                        assert start <= end and abs(end - time.time_ns()) < 60_000_000_000, (start, end)
            assert len(result.stdout) > 40, 'Synthetic snapshot produced no frame'
        # Sampling off suppresses hot spans but leaves their metrics intact.
        server.clear()
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
                            ('--otel-endpoint', 'ftp://collector.example'), ('--otel-log-level', 'invalid')]:
            args = base + ['--otel-endpoint', f'http://127.0.0.1:{server.server_port}', flag, value] + media
            result = subprocess.run(args, env=env, capture_output=True, timeout=10)
            assert result.returncode != 0, (flag, value)
        # TOML config, environment and CLI must resolve in increasing precedence.
        with tempfile.TemporaryDirectory() as temp:
            config = Path(temp) / 'otel.toml'
            config.write_text('[otel]\nendpoint = "http://127.0.0.1:%d"\nenabled_telemetry = "metrics"\nrealtime_sample_rate = 0.0\n' % server.server_port)
            server.clear()
            result = subprocess.run(base + ['--config', str(config)] + media,
                                    env=env, capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
            assert {p for p, _ in server.records} == {'/v1/metrics'}, [p for p, _ in server.records]
            server.clear()
            override_env = dict(env, ASCII_CHAT_OTEL_ENABLED_TELEMETRY='logs')
            result = subprocess.run(base + ['--config', str(config)] + media,
                                    env=override_env, capture_output=True, timeout=20)
            assert result.returncode == 0
            assert {p for p, _ in server.records} == {'/v1/logs'}, [p for p, _ in server.records]
            server.clear()
            result = subprocess.run(base + ['--config', str(config), '--otel-enabled-telemetry', 'traces'] + media,
                                    env=override_env, capture_output=True, timeout=20)
            assert result.returncode == 0
            assert {p for p, _ in server.records} == {'/v1/traces'}, [p for p, _ in server.records]
        if sys.platform == 'win32':
            server.clear()
            fixture = Path(binary).with_name('test-audio-priority.exe')
            result = subprocess.run([str(fixture)], env=dict(env,
                ASCII_CHAT_OTEL_FIXTURE_ENDPOINT=f'http://127.0.0.1:{server.server_port}'),
                capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
            metrics = b''.join(body for path, body in server.records if path == '/v1/metrics')
            for name in [b'ascii-chat.audio.underruns', b'ascii-chat.audio.overruns',
                         b'ascii-chat.audio.samples.dropped', b'ascii-chat.audio.buffer.fill',
                         b'ascii-chat.audio.jitter.target']:
                assert name in metrics, name
            for metric in signal_records(server.records, 'metrics'):
                if metric[1] in [[b'ascii-chat.audio.underruns'], [b'ascii-chat.audio.overruns'],
                                 [b'ascii-chat.audio.samples.dropped']]:
                    point = protobuf(protobuf(metric[7][0])[1][0])
                    assert struct.unpack('<d', point[4][0])[0] >= 1
        network_smoke(binary, server, env)
        database_smoke(binary, server, env)
        discovery_trace_smoke(binary, server)
        interrupted_collector_smoke(binary, env)
        print('ascii-chat OTLP integration: signals, media, sampling, offline collector, config precedence, encrypted networking and SQLite passed')
    finally:
        server.close()

if __name__ == '__main__':
    sys.exit(main())
