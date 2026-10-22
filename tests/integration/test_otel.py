"""Native synthetic media integration against a real OTLP/HTTP receiver."""
import os
from pathlib import Path
import subprocess
import sys
import time
import tempfile
import json
import socket
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[2] /
    'deps/ascii-chat-deps/gotel/c-gotel/testing/python'))
from gotel_testing import Collector, protobuf, records, service_names


def signal_records(requests, signal):
    return records(requests, '/v1/' + signal)


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
             '--audio=false', '--snapshot', '--snapshot-delay', '2', '--width', '40', '--height', '12',
             '--splash-screen=false'], env=fixture_env, capture_output=True, timeout=20)
        assert result.returncode == 0, result.stderr.decode(errors='replace')[-3000:]
        assert len(result.stdout) > 40
        traces = b''.join(body for path, body in receiver.records if path == '/v1/traces')
        assert b'crypto_handshake_client_key_exchange' in traces
        assert b'ascii-chat.server.video.render' in traces
        metrics = b''.join(body for path, body in receiver.records if path == '/v1/metrics')
        for name in [b'ascii-chat.network.sent', b'ascii-chat.network.received',
                     b'ascii-chat.network.packet.sent_size', b'ascii-chat.network.packet.received_size',
                     b'ascii-chat.network.round_trip_time']:
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
                            b'ascii-chat.storage.SchemaSessions', b'ascii-chat.storage.SchemaIdxRateEvents'])
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
        finally:
            process.terminate()
            process.wait(timeout=5)

def main():
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
        print('ascii-chat OTLP integration: signals, media, sampling, offline collector, config precedence, encrypted networking and SQLite passed')
    finally:
        server.close()

if __name__ == '__main__':
    main()
