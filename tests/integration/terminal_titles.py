#!/usr/bin/env python3
"""Smoke-test real terminal titles through ConPTY/PTY.

Requires the same pyte + pywinpty/pexpect dependencies as terminal_ui.py.
Run: python tests/integration/terminal_titles.py --binary build/bin/ascii-chat.exe
"""
import argparse
import os
from pathlib import Path
import re
import socket
import subprocess
import time

from terminal_ui import Terminal, free_port

OSC_TITLE = re.compile(r"\x1b\](?:0|2);([^\x07\x1b]*)(?:\x07|\x1b\\)")


class TitleTerminal(Terminal):
    def __init__(self, *args, **kwargs):
        self.titles = []
        self.title_count = 0
        super().__init__(*args, **kwargs)

    def pump(self, seconds=0.1):
        text = super().pump(seconds)
        titles = OSC_TITLE.findall("".join(self.raw))
        self.titles.extend((time.monotonic(), title) for title in titles[self.title_count:])
        self.title_count = len(titles)
        return text

    def expect_title(self, expected, timeout=12):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.pump()
            if self.titles and self.titles[-1][1] == expected:
                return self.titles[-1][0]
        raise AssertionError(f"Expected {expected!r}; observed {self.titles!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=Path('build/title-smoke'))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    terminals = []

    def terminal(name, command, **kwargs):
        term = TitleTerminal(command, rows=35, cols=110, terminal_replies=True, **kwargs)
        terminals.append((name, term))
        return term

    def mirror(unicode=True):
        return [binary, '--no-check-update', 'mirror', '--test-pattern', '--audio=false',
                '--splash-screen=false', '--width', '80', '--height', '24', '--fps', '15',
                '--utf8', 'true' if unicode else 'false']

    try:
        for unicode in (True, False):
            sep = ' • ' if unicode else ' - '
            base = 'ascii-chat' + sep + 'mirror'
            term = terminal('unicode' if unicode else 'ascii', mirror(unicode))
            first = term.expect_title('ascii-chat' + sep + 'video chat in your terminal')
            mode_time = term.expect_title(base)
            assert mode_time - first >= 2.6, 'Startup title disappeared before three seconds'
            term.write(']')
            term.expect_title(base + sep + 'log viewer')
            term.write('?')
            term.expect_title(base + sep + 'log viewer' + sep + 'help')
            term.write('?')
            term.expect_title(base + sep + 'log viewer')
            term.write(']')
            term.expect_title(base)
            term.write('=')
            term.expect_title(base + sep + 'statistics')
            term.write('?')
            term.expect_title(base + sep + 'statistics' + sep + 'help')
            term.write('?')
            term.expect_title(base + sep + 'statistics')
            term.write('=')
            term.expect_title(base)
            term.write('`')
            term.expect_title(base + sep + 'errors')
            term.write('?')
            term.expect_title(base + sep + 'errors' + sep + 'help')
            term.write('?')
            term.expect_title(base + sep + 'errors')
            term.write('`')
            term.expect_title(base)
            term.write('0')
            term.expect_title(base + sep + 'sync primitives')
            term.write('?')
            term.expect_title(base + sep + 'sync primitives' + sep + 'help')
            term.write('?')
            term.expect_title(base + sep + 'sync primitives')
            term.write('0')
            term.expect_title(base)
            term.write('o')
            term.expect_title(base + sep + 'load file/url')
            term.write('?')
            term.expect_title(base + sep + 'load file/url' + sep + 'help')
            term.write('?')
            term.expect_title(base + sep + 'load file/url')
            term.write('\x1b')
            term.expect_title(base)
            term.resize(3, 12)
            term.expect_title(base + sep + 'terminal too small')
            term.resize(35, 110)
            term.expect_title(base)
            if not unicode:
                assert all(title.isascii() for _, title in term.titles if title.startswith('ascii-chat'))
            assert all('\x1b' not in title for _, title in term.titles)
            term.interrupt()
            term.pump()
            if os.name == 'nt':
                assert not term.titles[-1][1].startswith('ascii-chat'), 'Previous console title was not restored'
            else:
                assert '\x1b[22;0t' in ''.join(term.raw) and '\x1b[23;0t' in ''.join(term.raw)
            print(f'PASS {"UTF-8" if unicode else "ASCII"} startup, screens, nested help, restoration, resize')
            term.close()

        # A TCP peer that accepts no protocol data keeps the client in its handshake.
        # Crossing the deadline alone must not replace the startup title.
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            pending = terminal('not-ready', [binary, '--no-check-update', 'client',
                               f'127.0.0.1:{listener.getsockname()[1]}',
                               '--test-pattern', '--audio=false', '--utf8', 'false'])
            pending.expect_title('ascii-chat - video chat in your terminal')
            pending.pump(3.4)
            app_titles = [title for _, title in pending.titles if title.startswith('ascii-chat')]
            assert app_titles and set(app_titles) == {'ascii-chat - video chat in your terminal'}, app_titles
            assert pending.process.isalive(), 'Handshake exited before readiness could be checked'
            pending.interrupt()
            pending.close()
        print('PASS startup title waits for readiness beyond three seconds')

        server = terminal('server', [binary, '--no-check-update', 'server', '--port', str(free_port()),
                                     '--status-screen=false', '--utf8', 'false'])
        server.expect_title('ascii-chat - video chat in your terminal')
        server.expect_title('ascii-chat - server')
        server.interrupt()
        server.close()
        print('PASS server title timer without status screen')

        for unicode in (True, False):
            destination = args.output.resolve() / ('unicode.mp4' if unicode else 'ascii.mp4')
            term = terminal('render-unicode' if unicode else 'render-ascii',
                            mirror(unicode) + ['--render-file', str(destination)])
            prefix = 'ascii-chat' + (' • ' if unicode else ' - ') + 'mirror' + (' • ' if unicode else ' - ')
            term.expect(lambda _: any(title.startswith(prefix + 'rendering file... ') for _, title in term.titles),
                        'Rendering title did not appear')
            term.pump(0.7)
            spinners = {title.removeprefix(prefix + 'rendering file... ') for _, title in term.titles
                        if title.startswith(prefix + 'rendering file... ')}
            assert len(spinners) >= 2, f'Spinner did not animate: {spinners}'
            allowed = set('⠋⠙⠹⠸⠼⠴⠦⠧' if unicode else '|/-\\')
            assert all(glyph in allowed for glyph in spinners), spinners
            term.interrupt()
            term.close()
            assert destination.stat().st_size > 1000, 'No encoded video produced'
            print(f'PASS {"UTF-8" if unicode else "ASCII"} rendering spinner and encoder shutdown')

        result = subprocess.run(mirror(False) + ['--snapshot', '--snapshot-delay', '0'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 0, result.stderr.decode(errors='replace')[-2000:]
        assert result.stdout.strip(), 'No snapshot output'
        assert b'\x1b]0;' not in result.stdout + result.stderr
        assert b'\x1b]2;' not in result.stdout + result.stderr
        short = subprocess.run([binary, '--help'], capture_output=True, timeout=5)
        assert short.returncode == 0
        assert b'\x1b]2;' not in short.stdout + short.stderr
        print('PASS redirected snapshot and short commands contain no title escapes')
    finally:
        for name, term in terminals:
            (args.output / (name + '.txt')).write_text(''.join(term.raw), encoding='utf-8')
            (args.output / (name + '-titles.txt')).write_text(
                '\n'.join(f'{stamp:.3f} {title}' for stamp, title in term.titles), encoding='utf-8')
            term.close()


if __name__ == '__main__':
    main()
