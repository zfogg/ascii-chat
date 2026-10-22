"""Prove compression and codec telemetry from real production round trips."""
from pathlib import Path
import os
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'deps/ascii-chat-deps/gotel/c-gotel/testing/python'))
from gotel_testing import Collector, records, protobuf

def attributes(span):
    result = {}
    for raw in span.get(9, []):
        attr = protobuf(raw); value = protobuf(attr[2][0])
        result[attr[1][0].decode()] = next(iter(value.values()))[0]
    return result
with Collector() as collector:
    env = {k:v for k,v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    env['ASCII_CHAT_OTEL_FIXTURE_ENDPOINT'] = collector.url
    result = subprocess.run([sys.argv[1]], env=env, capture_output=True, timeout=30)
    assert result.returncode in (0, 77), result.stderr.decode(errors='replace')[-5000:]
    spans = records(collector.records, '/v1/traces')
    by_name = {}
    for span in spans: by_name.setdefault(span[5][0].decode(), []).append(span)
    for operation in ['compress','compress_workspace','decompress','decompress_exact']:
        matched = by_name['ascii-chat.compression.zstd.'+operation]
        assert all(attributes(span)['compression.algorithm'] == b'zstd' for span in matched)
    exact = by_name['ascii-chat.compression.zstd.decompress_exact']
    assert any(protobuf(status).get(3) == [2] for s in exact for status in s.get(15, [])), 'size mismatch must mark span as error'
    metrics = {m[1][0].decode(): m for m in records(collector.records, '/v1/metrics')}
    def number(name, kind=7):
        return struct.unpack('<d', protobuf(protobuf(metrics['ascii-chat.'+name][kind][0])[1][0])[4][0])[0]
    assert number('compression.zstd.input.bytes') == 6144
    assert number('decompression.zstd.output.bytes') == 6144, 'failed exact decode must not add successful bytes'
    assert number('compression.zstd.output.bytes') > 0
    for name in ['compression.zstd.output_to_input_ratio']:
        assert 'ascii-chat.'+name in metrics


    if result.returncode == 77:
        print('Compression passed; video portion skipped: this FFmpeg lacks libx264/libx265')
    else:
        encodes = by_name['ascii-chat.video.encode']
        assert {attributes(s)['video.codec'] for s in encodes} == {b'h264', b'h265'}
        assert all(attributes(s)['video.frame.width'] == 32 and attributes(s)['video.frame.height'] == 32 for s in encodes)
        assert all(attributes(s)['video.packet.size'] > 5 for s in encodes)
        assert attributes(by_name['ascii-chat.video.decode'][0])['video.codec'] == b'h265'
        assert number('video.encode.input.bytes') == 6144
        assert number('video.encode.packets') == 2
        assert number('video.decode.frames') == 1
        assert number('video.decode.output.bytes') == 1024
        for name in ['video.encode.input.size', 'video.decode.packet.size']:
            assert 'ascii-chat.'+name in metrics
        print('H.264/H.265 codec telemetry passed')
