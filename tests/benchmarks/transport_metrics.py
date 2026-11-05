#!/usr/bin/env python3
"""Measure encrypted production TCP, WebRTC and QUIC packet delivery.

Run --netem only inside an isolated Linux network namespace/container with
CAP_NET_ADMIN. It temporarily changes that namespace's loopback qdisc.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401
import argparse
import json
import hashlib
import os
import platform
import re
import subprocess
import threading
import time


def command(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def wire_bytes():
    return int(Path('/sys/class/net/lo/statistics/tx_bytes').read_text())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--netem', action='store_true')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--transports', default='tcp,webrtc,quic')
    parser.add_argument('--source-revision', default='working-tree')
    parser.add_argument('--loader', type=Path, help='Optional bundled Linux loader; libraries must be beside it')
    parser.add_argument('--visual-bytes', type=int, default=32768)
    args = parser.parse_args()
    if args.runs < 1:
        parser.error('--runs must be positive')
    if not 1024 <= args.visual_bytes <= 262144:
        parser.error('--visual-bytes must be between 1024 and 262144')
    if platform.system() != 'Linux':
        parser.error('The measurement harness requires Linux resource and loopback counters')
    args.output.mkdir(parents=True, exist_ok=True)
    args.output = args.output.resolve()
    cert, key = args.output / 'cert.pem', args.output / 'key.pem'
    command('openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256',
            '-nodes', '-days', '1', '-subj', '/CN=localhost', '-addext',
            'subjectAltName=DNS:localhost,IP:127.0.0.1', '-keyout', str(key), '-out', str(cert))
    tc = ['tc'] if os.geteuid() == 0 else ['sudo', '-n', 'tc']
    if args.netem:
        qdisc = command(*tc, 'qdisc', 'show', 'dev', 'lo')
        if qdisc.strip() and 'qdisc noqueue' not in qdisc:
            raise RuntimeError(f'Refusing to replace an existing loopback qdisc: {qdisc}')
    records = []
    result_path = args.output / 'summary.json'
    profiles = ['clean', 'impaired'] if args.netem else ['clean']
    env = os.environ.copy()
    env.update(TESTING='1', LSAN_OPTIONS='detect_leaks=0')
    for run in range(args.runs):
        for profile in profiles:
            for mode in args.transports.split(','):
                timer = None
                process = None
                changed = False
                entry = {'run': run + 1, 'transport': mode, 'profile': profile,
                         'delivery': 'audio/visual datagrams' if mode == 'quic-datagrams' else 'reliable'}
                lines = []
                removed = []
                qdisc_stats = []
                def recover():
                    qdisc_stats.append(command(*tc, '-s', 'qdisc', 'show', 'dev', 'lo'))
                    command(*tc, 'qdisc', 'del', 'dev', 'lo', 'root')
                    removed.append(time.monotonic())
                try:
                    if profile == 'impaired':
                        command(*tc, 'qdisc', 'add', 'dev', 'lo', 'root', 'netem',
                                'limit', '200', 'delay', '20ms', '5ms', 'distribution', 'normal',
                                'loss', '1%', 'rate', '8mbit', 'seed', '247')
                        changed = True
                    before_bytes = wire_bytes()
                    started = time.monotonic()
                    launch = [str(args.binary.resolve()), mode, str(cert), str(key), str(args.visual_bytes)]
                    if args.loader:
                        launch = [str(args.loader.resolve()), '--library-path', str(args.loader.resolve().parent), *launch]
                    process = subprocess.Popen(launch,
                                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
                    def stop_timeout(child=process):
                        child.kill()
                    timeout = threading.Timer(60, stop_timeout)
                    timeout.start()
                    try:
                        for line in process.stdout:
                            lines.append(line)
                            if 'METRICS_READY' in line:
                                entry['ready_seconds'] = time.monotonic() - started
                                if changed:
                                    timer = threading.Timer(5, recover)
                                    timer.start()
                        process.stdout.close()
                        _, status, usage = os.wait4(process.pid, 0)
                        code = os.waitstatus_to_exitcode(status)
                        process.returncode = code
                    finally:
                        timeout.cancel()
                        timeout.join()
                    elapsed = time.monotonic() - started
                    after_bytes = wire_bytes()
                    entry.update(exit_code=code, elapsed_seconds=elapsed,
                                 cpu_seconds=usage.ru_utime + usage.ru_stime,
                                 wire_bytes=after_bytes - before_bytes)
                    if removed:
                        entry['impairment_removed_after_ready_seconds'] = removed[0] - started - entry['ready_seconds']
                        entry['qdisc_before_removal'] = qdisc_stats[0]
                    text = ''.join(lines)
                    log = args.output / f'{profile}-{mode}-{run+1}.log'
                    log.write_text(text)
                    setup = re.search(r'METRICS_SETUP transport=[\w-]+ ms=([\d.]+)', text)
                    entry['setup_ms'] = float(setup[1]) if setup else None
                    queue = re.search(r'METRICS_QUEUE peak_bytes=(\d+) limit_bytes=(\d+)', text)
                    entry['queue'] = {'peak': int(queue[1]), 'limit': int(queue[2])} if queue else None
                    entry['samples'] = [dict((k, float(v)) for k, v in re.findall(r'(\w+)=([\d.]+)', line))
                                        for line in lines if 'METRICS phase=' in line]
                    records.append(entry)
                    result_path.write_text(json.dumps({'platform': platform.platform(),
                        'source_revision': args.source_revision,
                        'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
                        'workload': f'control 32B/100ms, audio 960B/20ms, visual {args.visual_bytes}B/33ms',
                        'impairment': '20ms +/-5ms normal delay, 1% loss, 8Mbit/s, limit200, seed247; removed 5s after ready',
                        'records': records}, indent=2) + '\n')
                    print(json.dumps(entry), flush=True)
                    if (code or len(entry['samples']) != 6 or (changed and not removed) or
                            (profile == 'clean' and any(s['count'] == 0 for s in entry['samples']))):
                        raise RuntimeError(f'Measurement failed: {log}\n{text[-5000:]}')
                finally:
                    if process and process.poll() is None:
                        process.kill()
                        process.wait()
                    if timer:
                        timer.cancel()
                        timer.join()
                    if changed and not removed:
                        command(*tc, 'qdisc', 'del', 'dev', 'lo', 'root')


if __name__ == '__main__':
    main()
