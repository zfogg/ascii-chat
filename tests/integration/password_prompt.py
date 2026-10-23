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

SECRET = 's3\ncret!'
SHIFT_ENTER = '\x1b[13;2u'


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
    cw = round(font.getlength('M'))
    top, bottom = font.getbbox('\u2502')[1::2]
    ch = bottom - top
    image = Image.new('RGB', (term.screen.columns * cw, term.screen.lines * ch), '#000000')
    draw = ImageDraw.Draw(image)
    glyphs = []
    for row in range(term.screen.lines):
        for col in range(term.screen.columns):
            cell = term.screen.buffer[row][col]
            fg, bg = color(cell.fg, '#dddddd'), color(cell.bg, '#000000')
            if cell.reverse:
                fg, bg = bg, fg
            x, y = col * cw, row * ch
            draw.rectangle((x, y, x + cw - 1, y + ch - 1), fill=bg)
            glyphs.append((x, y, cell.data, fg))
    for x, y, text, fg in glyphs:
        draw.text((x, y), text, font=font, fill=fg)
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
            env = {**environment, **({'ASCII_CHAT_PASSWORD': SECRET} if password else {})}
            term = Terminal([binary, '--no-check-update', '--log-level', 'info', '--log-file',
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
        def selected(term, label):
            def highlighted(_):
                for row, line in enumerate(term.screen.display):
                    if label in line:
                        start = line.index(label)
                        cells = [term.screen.buffer[row][col] for col in range(start, start + len(label))]
                        backgrounds = all(cell.bg != 'default' and not cell.reverse and cell.fg != cell.bg for cell in cells)
                        bg = cells[0].bg
                        if len(bg) == 6 and all(c in '0123456789abcdef' for c in bg):
                            red, green, blue = (int(bg[i:i + 2], 16) for i in (0, 2, 4))
                            return backgrounds and (green > red and green > blue if 'Confirm' in label else red > green and red > blue)
                        return backgrounds and bg in (('green', 'brightgreen') if 'Confirm' in label else ('red', 'brightred'))
                return False
            term.expect(highlighted, 'Selected button must have a background fill and regular foreground')
        def enter_secret(term):
            term.write('s3' + SHIFT_ENTER + 'ct!' + '\x1b[D' * 2 + '\x1b[200~re\x1b[201~')
            term.expect(lambda s: '**' in s and '*****' in s, 'Multiline password was not masked')
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
            def client(name, extra=()):
                return terminal(name, ['client', '127.0.0.1', '--port', str(port), *common, *extra])
            peer = client('retry')
            prompt(peer)
            peer.expect(lambda _: bool(re.search(r'\x1b\][02];[^\x07\x1b]*password required', ''.join(peer.raw))),
                        'Password dialog terminal title missing')
            border = next(i for i, line in enumerate(peer.screen.display) if '\u250c' in line)
            assert abs(border - (peer.screen.lines - border - 14)) <= 1, 'Popup is not vertically centered'
            title_line = peer.screen.display[border]
            assert 'Password required' in title_line, 'Title is not embedded in the top border'
            for text in ('Password required', 'This server requires a password.'):
                row, line = next((r, line) for r, line in enumerate(peer.screen.display) if text in line)
                column = line.index(text)
                assert abs(2 * column + len(text) - peer.screen.columns) <= 1, f'{text} is not centered'
            row, line = next((r, line) for r, line in enumerate(peer.screen.display) if 'Tab select' in line)
            cells = peer.screen.buffer[row]
            assert cells[line.index('Tab')].fg != cells[line.index('select')].fg, 'Shortcut is not colored separately'
            selected(peer, '[ Confirm ]')
            peer.write('\t')
            selected(peer, '[ Cancel ]')
            save(peer, 'client-selected-cancel')
            peer.write('\t')
            selected(peer, '[ Confirm ]')
            save(peer, 'client-initial')
            peer.write('x' * 80)
            peer.expect(lambda s: s.count('*') >= 50, 'Long input did not render')
            save(peer, 'client-scrolled')
            end_x = peer.screen.cursor.x
            assert peer.screen.display[peer.screen.cursor.y].count('*') < 80, 'Long input did not scroll'
            peer.write('\x1b[H')
            peer.expect(lambda _: peer.screen.cursor.x < end_x - 20, 'Home did not reveal input start')
            home_x = peer.screen.cursor.x
            peer.write('\x1b[C')
            peer.expect(lambda _: peer.screen.cursor.x == home_x + 1, 'Right did not move input cursor')
            selected(peer, '[ Confirm ]')
            peer.write('\x1b[D')
            peer.expect(lambda _: peer.screen.cursor.x == home_x, 'Left did not move input cursor')
            peer.write('\x1b[F')
            peer.expect(lambda _: peer.screen.cursor.x == end_x, 'End did not reveal input tail')
            peer.write('\b' * 80)
            peer.expect(lambda s: '*' not in s, 'Scrolled input did not delete correctly')
            peer.write('short\r')
            peer.expect(lambda s: '*****' in s and 'Password required' in s, 'Short input submitted')
            peer.write('\b' * 5)
            peer.expect(lambda s: '*' not in s, 'Backspace editing failed')
            multiline = 'first\nsecond\nthird\nfourth'
            peer.write(SHIFT_ENTER.join(multiline.split('\n')))
            peer.expect(lambda s: sum('*' in line for line in s.splitlines()) == 4, 'Multiline edit did not render')
            peer.write(SHIFT_ENTER)
            peer.expect(lambda s: 'Maximum 3 newlines.' in s, 'Fourth newline was not rejected')
            borders = [(row, line) for row, line in enumerate(peer.screen.display) if '\u250c' in line or '\u2514' in line]
            assert borders[2][0] - borders[1][0] == 5, 'Input did not expand to four lines'
            assert borders[3][0] - borders[0][0] == 16, 'Popup did not grow with the input'
            save(peer, 'client-newline-limit')
            peer.resize(19, 40)
            peer.expect(lambda s: 'Maximum 3 newlines.' in s and sum('*' in line for line in s.splitlines()) == 4,
                        'Four-line password did not fit the compact terminal')
            peer.resize(30, 100)
            prompt(peer)
            peer.write('\b' * len(multiline))
            peer.expect(lambda s: '*' not in s and 'Maximum 3 newlines.' not in s, 'Multiline deletion did not shrink input')
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
            enter_secret(peer)
            selected(peer, '[ Confirm ]')
            peer.write('\t')
            selected(peer, '[ Cancel ]')
            save(peer, 'client-selected-cancel')
            peer.write('\t')
            selected(peer, '[ Confirm ]')
            peer.pump(.3)
            save(peer, 'client-masked')
            peer.write('\r')
            peer.expect(lambda s: 'Password required' not in s and len(s.strip()) > 100,
                        'Correct password did not connect', timeout=30)
            deadline = time.monotonic() + 10
            while 'CLIENT_CONNECTION_RUNNING' not in (root / 'retry.log').read_text(encoding='utf-8', errors='replace'):
                assert time.monotonic() < deadline, peer.pump()
                peer.pump(.1)
            assert SECRET not in ''.join(peer.raw)
            for name, keys, utf8 in [('escape', '\x1b', 'true'), ('cancel', '\t\r', 'true'),
                                     ('ascii-cancel', '\t\r', 'false')]:
                cancelled = client(name, ['--utf8', utf8])
                prompt(cancelled)
                text = cancelled.pump(.1)
                assert ('\u250c' in text) == (utf8 == 'true'), 'Incorrect UTF-8 border policy'
                if utf8 == 'false':
                    assert '+' in text and '|' in text
                    save(cancelled, 'client-ascii')
                cancelled.write(keys)
                wait_exit(cancelled)
            print('PASS client: retries, editing, spaces, masking, minimum length, multiline limit, UTF-8/ASCII borders, Tab, cursor navigation, horizontal scrolling, resize, Enter, Esc', flush=True)

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
            enter_secret(joiner)
            joiner.write('\r')
            joiner.expect(lambda s: 'Password required' not in s, 'Discovery did not resume')
            deadline = time.monotonic() + 10
            while 'Discovery state: CONNECTING_HOST' not in (root / 'discovery-retry.log').read_text(encoding='utf-8', errors='replace'):
                assert time.monotonic() < deadline, joiner.pump()
                joiner.pump(.1)
            assert joiner.process.isalive()
            assert SECRET not in ''.join(joiner.raw)
            for name, keys in [('discovery-escape', '\x1b'), ('discovery-cancel', '\t\r')]:
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
                assert SECRET not in text, f'Password leaked in {log.name}'


if __name__ == '__main__':
    main()
