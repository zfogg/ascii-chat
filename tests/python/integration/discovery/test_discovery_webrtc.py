"""Discovery hosting and encrypted WebRTC mixed video on loopback."""
import re
import time

import pytest
from ascii_chat_tests.process import Service, free_port

pytestmark = [pytest.mark.integration, pytest.mark.network]


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


@pytest.mark.parametrize("audio", [False, True])
def test_discovery_webrtc(binary, tmp_path, request, audio):
    exercise(binary, tmp_path, audio=audio, debug_ice=request.config.getoption("--discovery-debug-ice"),
             ipv6=request.config.getoption("--discovery-ipv6"))
