import pytest
from types import SimpleNamespace
"""Run real transport saturation/recovery fixtures and retain CSV, logs and JSON.

Build test-backpressure-live first. Receiver reads are paced for TCP/WebSocket; a
verified UDP relay paces WebRTC packets. A second connection remains unthrottled.
Example: uv run --project tests/ascii_chat_tests --frozen python -m pytest tests/python/integration/backpressure --backpressure-probe build/bin/test-backpressure-live --backpressure-output build/backpressure
"""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import platform
from datetime import datetime, timezone

pytestmark = [pytest.mark.integration, pytest.mark.network]


def test_transport_backpressure(backpressure_probe, tmp_path, request):
    output = request.config.getoption("--backpressure-output")
    args = SimpleNamespace(binary=backpressure_probe, output=Path(output) if output else tmp_path,
                           transports=request.config.getoption("--backpressure-transports"),
                           codecs=request.config.getoption("--backpressure-codecs"))
    args.binary = args.binary.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {'platform': platform.platform(), 'generated_at_utc': datetime.now(timezone.utc).isoformat(), 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(), 'scenarios': []}
    source_root = Path(__file__).resolve().parents[4]
    try:
        summary['source_sha'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL, cwd=source_root, timeout=30).strip()
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, OSError):
        summary['source_sha'] = 'source archive'
    source_digest = hashlib.sha256()
    for directory in ['include', 'lib', 'src', 'tests/standalone/network']:
        for source in sorted((source_root / directory).rglob('*')):
            if source.is_file() and source.suffix in ['.c', '.h', '.cpp', '.hpp'] and source.relative_to(source_root).as_posix() != 'include/ascii-chat/version.h':
                source_digest.update(source.relative_to(source_root).as_posix().encode())
                source_digest.update(source.read_bytes().replace(b'\r\n', b'\n'))
    summary['native_source_sha256'] = source_digest.hexdigest()
    summary['source_state'] = 'working tree snapshot; binary and native source fingerprints identify the tested build'
    failed = False
    for transport in args.transports.split(','):
        for codec in args.codecs.split(','):
            name = f'{transport}-{codec}'
            csv = (args.output / (name + '.csv')).resolve()
            started = time.monotonic()
            try:
                result = subprocess.run([str(args.binary), transport, codec, str(csv)],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
                text = result.stdout.decode('utf-8', errors='replace')
                code = result.returncode
            except subprocess.TimeoutExpired as error:
                text = (error.stdout or b'').decode('utf-8', errors='replace') + '\nFixture timed out after 120s.\n'
                code = -1
            (args.output / (name + '.log')).write_text(text, encoding='utf-8')
            entry = {'transport': transport, 'codec': codec, 'exit_code': code,
                     'elapsed_seconds': round(time.monotonic() - started, 3), 'csv': csv.name}
            for label in ['RESULT', 'HEALTHY', 'RELAY', 'STEADY', 'DURATION', 'SOURCE']:
                match = re.search(r'\b' + label + r' (.*)', text)
                if match:
                    fields = dict(re.findall(r'(\w+)=([\w.\-]+)', match.group(1)))
                    entry[label.lower()] = {key: (float(value) if '.' in value else int(value))
                                            if re.fullmatch(r'[\d.]+', value) else value for key, value in fields.items()}
            if code or 'result' not in entry or 'healthy' not in entry:
                failed = True
                print(text, flush=True)
            summary['scenarios'].append(entry)
            print(json.dumps(entry), flush=True)
            (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    assert not failed, "One or more backpressure scenarios failed"
