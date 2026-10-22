"""Opt-in OTEL soak/pressure/outage checks. Never registered with CTest.

Run manually: python tests/manual/test_otel_load.py build/bin/ascii-chat --seconds 60
Reports the enabled/disabled timing ratio without imposing noisy CI thresholds.
"""
from pathlib import Path
import argparse
import json
import os
import struct
import subprocess
import sys
import time
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'deps/ascii-chat-deps/gotel/c-gotel/testing/python'))
from gotel_testing import Collector, protobuf, records

def points(requests, name, kind=5):
    return [protobuf(point) for metric in records(requests, '/v1/metrics') if metric[1] == [name.encode()]
            for point in protobuf(metric[kind][0]).get(1, [])]
def maximum(requests, name):
    return max((struct.unpack('<d', point[4][0])[0] for point in points(requests, name)), default=0)
def child_cpu(process=None):
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        times = [wintypes.FILETIME() for _ in range(4)]
        fn = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
        fn.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
        if not fn(wintypes.HANDLE(int(process._handle)), *(ctypes.byref(t) for t in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        return sum(((t.dwHighDateTime << 32) | t.dwLowDateTime) for t in times[2:]) / 1e7
    import resource
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    return usage.ru_utime + usage.ru_stime

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--seconds', type=int, default=60)
    args = parser.parse_args()
    if args.seconds < 2: parser.error('--seconds must be at least 2')
    binary = str(Path(args.binary).resolve())
    env = {k:v for k,v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    media = ['mirror', '--test-pattern', '--audio=false', '--snapshot', '--snapshot-delay', str(args.seconds),
             '--width', '80', '--height', '24', '--splash-screen=false']
    base = [binary, '--no-check-update']
    report = {}
    def execute(name, flags, callback=None):
        cpu_start = 0 if os.name == 'nt' else child_cpu()
        start = time.monotonic()
        log_dir = tempfile.TemporaryDirectory()
        log = Path(log_dir.name) / 'collector.log'
        process = subprocess.Popen(base + ['--log-file', str(log)] + flags + media, env=env,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            if callback: callback(process)
            _, stderr = process.communicate(timeout=args.seconds + 20)
            assert process.returncode == 0, (name, stderr[-2000:])
        finally:
            if process.poll() is None: process.kill(); process.wait()
        report[name] = {'wall_seconds': time.monotonic()-start, 'cpu_seconds': child_cpu(process)-cpu_start}
        messages = stderr + log.read_bytes()
        log_dir.cleanup()
        return messages
    execute('disabled', [])
    with Collector() as collector:
        execute('defaults', ['--otel-endpoint', collector.url])
        report['defaults']['spans_received'] = len(records(collector.records, '/v1/traces'))
        report['defaults']['payload_bytes'] = sum(len(body) for _, body in collector.records)
        report['defaults']['bytes_per_second'] = report['defaults']['payload_bytes'] / args.seconds
        collector.clear()
        flags = ['--otel-endpoint', collector.url, '--otel-realtime-sample-rate', '1',
                 '--otel-realtime-max-per-second', '100000', '--otel-metrics-interval', '100', '--otel-timeout', '500']
        execute('enabled', flags)
        assert records(collector.records, '/v1/traces') and records(collector.records, '/v1/logs')
        assert maximum(collector.records, 'ascii-chat.otel.records.sent') > 0
        report['enabled']['spans_received'] = len(records(collector.records, '/v1/traces'))
        report['enabled']['peak_rss_bytes'] = maximum(collector.records, 'ascii-chat.process.memory.resident')
        report['enabled']['drops'] = maximum(collector.records, 'ascii-chat.otel.records.dropped')
        collector.clear()
        execute('queue_pressure', flags + ['--otel-queue-capacity', '2'])
        report['queue_pressure']['spans_received'] = len(records(collector.records, '/v1/traces'))
        report['queue_pressure']['delivery_ratio'] = report['queue_pressure']['spans_received']/max(1, report['enabled']['spans_received'])
        assert report['queue_pressure']['delivery_ratio'] < .9, 'fixture did not reduce delivered spans under queue pressure'
        collector.clear()
        def recover(process):
            # Hold the collector unavailable through the first half of the run.
            time.sleep(args.seconds / 2)
            assert process.poll() is None
            for signal in ['traces', 'logs', 'metrics']: collector.script('/v1/'+signal, [])
        for signal in ['traces', 'logs', 'metrics']: collector.script('/v1/'+signal, [503]*10000)
        stderr = execute('outage_recovery', flags, recover)
        assert b'Telemetry collector export failed' in stderr, stderr[-2000:]
        assert b'Telemetry collector export recovered' in stderr, stderr[-2000:]
        assert any(status == 503 for _, _, status in collector.attempts)
        assert all(records(collector.records, '/v1/'+signal) for signal in ['traces', 'logs', 'metrics'])
        report['outage_recovery']['attempts'] = len(collector.attempts)
        collector.clear()
        for signal in ['traces', 'logs', 'metrics']: collector.script('/v1/'+signal, [503]*10000)
        execute('shutdown_during_outage', flags)
        assert report['shutdown_during_outage']['wall_seconds'] < args.seconds + 10
    report['enabled_cpu_ratio'] = report['enabled']['cpu_seconds']/max(.000001, report['disabled']['cpu_seconds'])
    report['enabled_wall_ratio'] = report['enabled']['wall_seconds']/report['disabled']['wall_seconds']
    print(json.dumps(report, indent=2))
if __name__ == '__main__': main()
