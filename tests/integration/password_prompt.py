"""Password dialog smoke tests against native TCP and discovery processes.

Optional PNGs render the observed terminal cells, preserving terminal colors.
"""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import _environment  # noqa: F401
import argparse
import os
import re
import shutil
import socket
import subprocess
import tempfile
import time
from terminal_ui import Terminal, free_port


def capture(term, path):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return  # Screenshots are optional; terminal assertions still run.
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf' if os.name == 'nt' else 'DejaVuSansMono.ttf', 20)
    palette = {'black': '#000000', 'red': '#cd3131', 'green': '#0dbc79', 'yellow': '#e5e510',
               'blue': '#2472c8', 'magenta': '#bc3fbc', 'cyan': '#11a8cd', 'white': '#e5e5e5',
               'brightblack': '#666666', 'brightred': '#f14c4c', 'brightgreen': '#23d18b',
               'brightyellow': '#f5f543', 'brightblue': '#3b8eea', 'brightmagenta': '#d670d6',
               'brightcyan': '#29b8db', 'brightwhite': '#ffffff'}
    def color(value, default):
        return palette.get(value, '#' + value if len(value) == 6 and value != 'default' else default)
    image = Image.new('RGB', (term.screen.columns * 12, term.screen.lines * 25), '#000000')
    draw = ImageDraw.Draw(image)
    for row in range(term.screen.lines):
        for col in range(term.screen.columns):
            cell = term.screen.buffer[row][col]
            fg, bg = color(cell.fg, '#dddddd'), color(cell.bg, '#000000')
            if cell.reverse:
                fg, bg = bg, fg
            x, y = col * 12, row * 25
            draw.rectangle((x, y, x + 11, y + 24), fill=bg)
            draw.text((x, y), cell.data, font=font, fill=fg)
    image.save(path)
    path.with_suffix('.txt').write_text('\n'.join(term.screen.display), encoding='utf-8')


def wait_exit(term):
    deadline = time.monotonic() + 12
    while term.process.isalive() and time.monotonic() < deadline:
        term.pump(.1)
    assert not term.process.isalive(), 'Cancellation did not exit after cleanup'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix='ascii-password-') as tmp:
        root = Path(tmp)
        common = ['--test-pattern', '--audio=false']
        # Identity prompts have separate coverage; this fixture isolates password entry.
        environment = dict(os.environ, TERM='xterm-256color', COLORTERM='truecolor',
                           ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1')
        for key in ('ASCII_CHAT_QUESTION_PROMPT_RESPONSE', 'ASCII_CHAT_PASSWORD'):
            environment.pop(key, None)
            os.environ.pop(key, None)
        terminals, services = [], []
        def terminal(name, mode_args, password=False):
            env = {**environment, **({'ASCII_CHAT_PASSWORD': 's3 cret!'} if password else {})}
            term = Terminal([binary, '--no-check-update', '--log-level', 'debug', '--log-file',
                             str(root / (name + '.log')), *mode_args], rows=30, cols=100,
                            terminal_replies=True, environment=env)
            terminals.append(term)
            return term
        def save(term, name):
            if args.artifacts:
                args.artifacts.mkdir(parents=True, exist_ok=True)
                capture(term, args.artifacts / (name + '.png'))
        def prompt(term):
            term.expect(lambda s: 'Password required' in s and '[ Cancel ]' in s, 'Missing password popup', timeout=30)
        def reject(term, answer, paste=False):
            term.write('\x1b[200~' + answer + '\x1b[201~' if paste else answer)
            term.expect(lambda s: '*' * len(answer) in s, 'Password was not entered')
            term.write('\r')
            term.expect(lambda s: 'Incorrect password. Try again.' in s and '***' not in s, 'Missing retry feedback')
        try:
            port = free_port()
            server = terminal('server', ['server', '--port', str(port), '--status-screen=false'], password=True)
            deadline = time.monotonic() + 15
            while True:
                try:
                    with socket.create_connection(('127.0.0.1', port), timeout=.2):
                        break
                except OSError:
                    assert time.monotonic() < deadline, server.pump()
                    server.pump(.1)
            def client(name):
                return terminal(name, ['client', '127.0.0.1', '--port', str(port), *common])
            peer = client('retry')
            prompt(peer)
            peer.expect(lambda _: bool(re.search(r'\x1b\][02];[^\x07\x1b]*password required', ''.join(peer.raw))),
                        'Password dialog terminal title missing')
            border = next(i for i, line in enumerate(peer.screen.display) if '\u250c' in line)
            assert abs(border - (peer.screen.lines - border - 13)) <= 1, 'Popup is not vertically centered'
            save(peer, 'client-initial')
            peer.write('short\r')
            peer.expect(lambda s: '*****' in s and 'Password required' in s, 'Short input submitted')
            peer.write('\x1b[H' + '\x1b[3~' * 5)
            peer.expect(lambda s: '*****' not in s, 'Home/Delete editing failed')
            reject(peer, 'wrongpass', paste=True)
            save(peer, 'client-incorrect')
            reject(peer, 'wrong again')
            peer.resize(10, 30)
            peer.expect(lambda s: 'Terminal too small' in s, 'Small terminal warning missing')
            peer.resize(19, 40)
            prompt(peer)
            save(peer, 'client-narrow')
            peer.resize(30, 100)
            prompt(peer)
            peer.write('s3 crex\b' + 't!')
            peer.expect(lambda s: '********' in s, 'Masked line editing failed')
            peer.write('\t')
            peer.pump(.3)
            save(peer, 'client-masked')
            peer.write('\r')
            peer.expect(lambda s: 'Password required' not in s and len(s.strip()) > 100,
                        'Correct password did not connect', timeout=30)
            deadline = time.monotonic() + 10
            while 'CLIENT_CONNECTION_RUNNING' not in (root / 'retry.log').read_text(encoding='utf-8', errors='replace'):
                assert time.monotonic() < deadline, peer.pump()
                peer.pump(.1)
            assert 's3 cret!' not in ''.join(peer.raw)
            for name, keys in [('escape', '\x1b'), ('cancel', '\t\x1b[200~discarded\x1b[201~\t\r')]:
                cancelled = client(name)
                prompt(cancelled)
                cancelled.write(keys)
                wait_exit(cancelled)
            print('PASS client: retries, editing, spaces, masking, minimum length, resize, confirm, Esc, Cancel', flush=True)

            acds_port = free_port()
            acds = subprocess.Popen([binary, '--no-check-update', '--log-file', str(root / 'acds.log'),
                                     'discovery-service', '--port', str(acds_port), '--websocket-port', str(free_port()),
                                     '--database', str(root / 'acds.db'), '--status-screen=false'],
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=environment)
            services.append(acds)
            time.sleep(1)
            assert acds.poll() is None, 'ACDS failed to start'
            discovery = ['--discovery-service', '127.0.0.1', '--discovery-service-port', str(acds_port),
                         '--port', str(free_port()), '--splash-screen=true', '--stun-servers', 'stun:127.0.0.1:9',
                         '--turn-servers', 'turn:127.0.0.1:9', *common]
            host = terminal('host', discovery, password=True)
            invitation = host.expect(lambda s: 'Run: ascii-chat ' in s, 'Host invitation missing', timeout=35)
            session = re.search(r'Run: ascii-chat ([a-z]+-[a-z]+-[a-z]+)', invitation).group(1)
            joiner = terminal('discovery-retry', [session, *discovery])
            prompt(joiner)
            save(joiner, 'discovery-initial')
            reject(joiner, 'wrongpass')
            save(joiner, 'discovery-incorrect')
            joiner.write('s3 cret!\r')
            joiner.expect(lambda s: 'Password required' not in s, 'Discovery did not resume')
            deadline = time.monotonic() + 10
            while 'Discovery state: CONNECTING_HOST' not in (root / 'discovery-retry.log').read_text(encoding='utf-8', errors='replace'):
                assert time.monotonic() < deadline, joiner.pump()
                joiner.pump(.1)
            assert joiner.process.isalive()
            assert 's3 cret!' not in ''.join(joiner.raw)
            for name, keys in [('discovery-escape', '\x1b'), ('discovery-cancel', '\t\t\r')]:
                cancelled = terminal(name, [session, *discovery])
                prompt(cancelled)
                cancelled.write(keys)
                wait_exit(cancelled)
            print('PASS discovery: missing password, wrong password, corrected session join, Esc, Cancel', flush=True)
        finally:
            for term in reversed(terminals):
                term.close()
            for service in services:
                service.terminate()
                service.wait(timeout=10)
            for log in root.glob('*.log'):
                text = log.read_text(encoding='utf-8', errors='replace')
                if args.artifacts:
                    args.artifacts.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(log, args.artifacts / log.name)
                assert 's3 cret!' not in text, f'Password leaked in {log.name}'


if __name__ == '__main__':
    main()
