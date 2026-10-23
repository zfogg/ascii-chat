"""Prove compression and codec telemetry from real production round trips."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import wave
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'deps/ascii-chat-deps/gotel/c-gotel/testing/python'))
from gotel_testing import Collector, records, protobuf, service_names

def attributes(span):
    result = {}
    for raw in span.get(9, []):
        attr = protobuf(raw); value = protobuf(attr[2][0])
        result[attr[1][0].decode()] = next(iter(value.values()))[0]
    return result
with Collector() as collector, tempfile.TemporaryDirectory() as temporary:
    env = {k:v for k,v in os.environ.items() if not k.startswith(('OTEL_', 'ASCII_CHAT_OTEL_'))}
    env['ASCII_CHAT_OTEL_FIXTURE_ENDPOINT'] = collector.url
    video = Path(temporary) / 'video.ppm'
    video.write_bytes(b'P6\n16 16\n255\n' + bytes([32, 64, 128]) * 256)
    audio = Path(temporary) / 'audio.wav'
    with wave.open(str(audio), 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(16000)
        wav.writeframes(struct.pack('<16000h', *([1000, -1000] * 8000)))
    env['ASCII_CHAT_OTEL_FIXTURE_VIDEO'] = str(video)
    env['ASCII_CHAT_OTEL_FIXTURE_AUDIO'] = str(audio)
    result = subprocess.run([sys.argv[1]], env=env, capture_output=True, timeout=30)
    assert result.returncode in (0, 77), result.stderr.decode(errors='replace')[-5000:]
    assert service_names(collector.records) == {'ascii-chat-mirror'}
    spans = records(collector.records, '/v1/traces')
    labels = [b'TRACE', b'DEBUG', b'INFO', b'WARN', b'ERROR', b'FATAL']
    fixture_logs = [log for log in records(collector.records, '/v1/logs')
                    if protobuf(log[5][0]).get(1, [b''])[0].startswith(b'otel severity fixture ')]
    assert len(fixture_logs) == 6
    for level, label in enumerate(labels):
        log = next(log for log in fixture_logs if protobuf(log[5][0])[1] == [f'otel severity fixture {level}'.encode()])
        assert log[2] == [1 + 4 * level] and log[3] == [label], log
    by_name = {}
    for span in spans: by_name.setdefault(span[5][0].decode(), []).append(span)
    for operation in ['input.open', 'demux.open', 'demux.probe', 'decoder.open', 'demux.read_packet',
                      'decoder.send_packet', 'decoder.receive_frame', 'video.decode', 'video.scale',
                      'audio.read', 'audio.resample', 'input.seek', 'input.close']:
        assert 'ascii-chat.ffmpeg.' + operation in by_name, operation
    assert attributes(by_name['ascii-chat.ffmpeg.demux.open'][0])['media.source.kind'] == b'file'
    assert any(protobuf(status).get(3) == [2]
               for span in by_name['ascii-chat.ffmpeg.demux.open'] for status in span.get(15, [])), 'missing input must mark demux open as failed'
    for name in ['ascii-chat.ffmpeg.demux.read_packet', 'ascii-chat.ffmpeg.decoder.receive_frame']:
        for span in by_name[name]:
            assert all(protobuf(status).get(3) != [2] for status in span.get(15, [])), 'EOF/EAGAIN must not be failures'
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
