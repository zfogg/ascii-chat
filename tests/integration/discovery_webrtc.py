"""Verify automatic discovery hosting and encrypted WebRTC mixed video on loopback."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import argparse
import contextlib
import re
import tempfile
import time
from ascii_chat_tests.process import Service, free_port


def exercise(binary, directory, audio=False, debug_ice=False, ipv6=False):
    ice_address = '::1' if ipv6 else '127.0.0.1'
    for suffix in ['ascii-chat', '.config/ascii-chat']:
        (directory/suffix).mkdir(parents=True, exist_ok=True)
    identity = directory/'identity'
    identity.write_bytes(bytes.fromhex('9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'))
    services = []
    try:
        ap, wp, hp = free_port(), free_port(), free_port()
        env = {'ASCII_CHAT_QUESTION_PROMPT_RESPONSE':'y;y;y', 'ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK':'1'}
        acds = Service(binary, directory, 'acds', 'discovery-service',
                       ['127.0.0.1', '--key', str(identity), '--port', str(ap), '--websocket-port', str(wp),
                        '--database', str(directory/'acds.db'), '--status-screen=false'], env)
        services.append(acds)
        acds.wait_for('Server accepting connections')
        common = ['--discovery-service', '127.0.0.1', '--discovery-service-port', str(ap),
                  '--width','40','--height','12','--color-mode','none','--splash-screen=false','--webrtc-skip-stun','--webrtc-disable-turn',
                  '--webrtc-bind-address', ice_address]
        common += (['--audio=true', '--test-audio', '--no-audio-playback'] if audio else ['--audio=false'])
        level = 'dev' if debug_ice else 'debug'
        host = Service(binary, directory, 'host', '--test-pattern', common + ['--port',str(hp)], env, log_level=level)
        services.append(host)
        host.wait_for('Session created:', timeout=25)
        name = re.search(r'Session created: ([a-z]+-[a-z]+-[a-z]+)', host.text())[1]
        print('Session',name, flush=True)
        client = Service(binary, directory, 'client', name, common + ['--test-pattern'], env, log_level=level)
        services.append(client)
        client.wait_for('WebRTC Media Channel ACTIVE', timeout=35)
        for peer in (host, client):
            # The selected bind must reach ICE itself, not only the TCP listener.
            peer.wait_for(f'Binding WebRTC ICE sockets to {ice_address}')
            for side in ('local', 'remote'):
                peer.wait_for(f'WebRTC selected {side} candidate:')
                selected = re.findall(rf'WebRTC selected {side} candidate: (.+)', peer.text())
                assert selected and all(f' {ice_address} ' in candidate for candidate in selected), selected
        host.wait_for('Frame received from client', timeout=20)
        if audio:
            client.wait_for('Discovery decoded remote audio: blocks=25', timeout=30)
            observations = re.findall(r'Discovery decoded remote audio: blocks=(\d+) samples=(\d+)', client.text())
            blocks, samples = map(int, observations[-1])
            assert blocks >= 25 and samples == blocks * 1920, observations
            print('STEREO AUDIO RECEIVED: 960 frames / 1920 samples per packet', flush=True)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            text = client.output.read_text(errors='replace')
            text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', text)
            rows = [row for row in text.splitlines() if len(row) == 40]
            # A mixed frame contains both participants separated by a vertical bar.
            mixed = [row for row in rows if '|' in row and row.split('|', 1)[0].strip()
                     and row.split('|', 1)[1].strip()]
            if len(mixed) >= 12:
                break
            if client.process.poll() is not None:
                raise AssertionError('Discovery client exited before displaying a mixed frame')
            time.sleep(0.02)
        else:
            raise AssertionError('No complete mixed ASCII frame was displayed')
        print('MEDIA RECEIVED', flush=True)
    finally:
        # Attempt every shutdown and propagate cleanup failures after all children stop.
        failures = []
        for service in reversed(services):
            try:
                service.stop()
            except Exception as error:
                failures.append(error)
        if failures:
            raise AssertionError('Discovery service cleanup failed: ' + '; '.join(map(str, failures)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--audio', action='store_true')
    parser.add_argument('--ipv6', action='store_true', help='Use IPv6 loopback for ICE with IPv4 signaling')
    parser.add_argument('--debug-ice', action='store_true')
    parser.add_argument('--artifacts-dir', type=Path)
    args = parser.parse_args()
    if args.artifacts_dir:
        args.artifacts_dir = args.artifacts_dir.resolve()
        args.artifacts_dir.mkdir(parents=True, exist_ok=True)
    artifacts = (contextlib.nullcontext(args.artifacts_dir) if args.artifacts_dir else
                 tempfile.TemporaryDirectory(prefix='ascii-discovery-webrtc-'))
    with artifacts as tmp:
        exercise(args.binary.resolve(), Path(tmp), audio=args.audio, debug_ice=args.debug_ice, ipv6=args.ipv6)
