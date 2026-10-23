"""Verify automatic discovery hosting and encrypted WebRTC mixed video on loopback."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

from pathlib import Path
import re
import sys
import tempfile
import time
from server_lifecycle import Service, free_port


def exercise(binary, directory):
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
                  '--audio=false', '--width','40','--height','12','--color-mode','none','--splash-screen=false','--webrtc-skip-stun','--webrtc-disable-turn']
        host = Service(binary, directory, 'host', '--test-pattern', common + ['--port',str(hp)], env)
        services.append(host)
        host.wait_for('Session created:', timeout=25)
        name = re.search(r'Session created: ([a-z]+-[a-z]+-[a-z]+)', host.text())[1]
        print('Session',name, flush=True)
        client = Service(binary, directory, 'client', name, common + ['--test-pattern'], env)
        services.append(client)
        client.wait_for('WebRTC Media Channel ACTIVE', timeout=35)
        host.wait_for('Frame received from client', timeout=20)
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
    with tempfile.TemporaryDirectory(prefix='ascii-discovery-webrtc-') as tmp:
        exercise(Path(sys.argv[1]).resolve(), Path(tmp))
