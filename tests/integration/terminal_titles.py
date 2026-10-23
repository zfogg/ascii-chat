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
from kitty_output import reconstruct

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

    def title_pattern(term, pattern, description, timeout=15):
        term.expect(lambda _: bool(term.titles and re.fullmatch(pattern, term.titles[-1][1])),
                    description, timeout=timeout)

    try:
        for unicode in (True, False):
            base = '🔠 ascii-chat • mirror' if unicode else ''
            term = terminal('unicode' if unicode else 'ascii', mirror(unicode))
            if unicode:
                first = term.expect_title('🔠 ascii-chat • video chat in your terminal')
                later = term.expect_title(base)
                assert later - first >= 2.6, 'Startup title ended too early'
            else:
                term.expect_title('')
                term.pump(2)
            for key, label in [(']', 'log viewer'), ('=', 'statistics'), ('`', 'errors'), ('0', 'sync primitives')]:
                expected = base + ' • ' + label if unicode else ''
                term.write(key)
                term.pump(0.2)
                term.expect_title(expected)
                before = len(term.titles)
                term.pump(0.3)
                assert len(term.titles) == before, 'Repeated title writes for an unchanged view'
                term.write('?')
                term.pump(0.2)
                term.expect_title(expected + ' • help' if unicode else '')
                term.write('?')
                term.pump(0.2)
                term.expect_title(expected)
                term.write(key)
                term.pump(0.2)
                term.expect_title(base)
            term.write('o')
            term.pump(0.2)
            term.expect_title(base + ' • load file/url' if unicode else '')
            term.write('?')
            term.pump(0.2)
            term.expect_title(base + ' • load file/url • help' if unicode else '')
            term.write('?')
            term.pump(0.2)
            term.write('\x1b')
            term.pump(0.2)
            term.expect_title(base)
            term.resize(3, 12)
            term.expect_title(base + ' • terminal too small' if unicode else '')
            term.resize(35, 110)
            term.expect_title(base)
            if not unicode:
                assert [title for _, title in term.titles] == [''], term.titles
            if os.name != 'nt':
                assert f'\x1b]0;{base}\x1b\\' in ''.join(term.raw)
            term.interrupt()
            term.pump()
            if os.name == 'nt':
                assert not term.titles[-1][1].startswith('🔠'), 'Previous console title was not restored'
            else:
                assert '\x1b[22;0t' in ''.join(term.raw) and '\x1b[23;0t' in ''.join(term.raw)
            print(f'PASS {"UTF-8" if unicode else "empty ASCII"} titles, nested views, deduplication, restoration')
            term.close()

        port = free_port()
        server = terminal('server', [binary, '--no-check-update', 'server', '--port', str(port),
                           '--status-screen=false', '--no-encrypt', '--no-auth', '--utf8', 'true'])
        server.expect(lambda _: 'Server accepting connections' in ''.join(server.raw), 'Server did not become ready', timeout=25)
        server.expect_title('🔠 ascii-chat • server')
        client = terminal('client-large-viewport', [binary, '--no-check-update', 'client', f'127.0.0.1:{port}',
                          '--test-pattern', '--audio=false', '--splash-screen=false', '--no-encrypt', '--no-auth', '--utf8', 'true'])
        client.expect(lambda text: any(len(row.rstrip()) > 80 and row.isascii() and re.search(r'(.)\1{7}', row)
                                      for row in text.splitlines()),
                      'Splash-disabled client did not render a frame wider than 80 columns', timeout=25)
        client.expect_title('🔠 ascii-chat • client')
        client.interrupt()
        client.close()
        server.interrupt()
        server.close()
        print('PASS server without status screen and splash-disabled client on a 110x35 terminal')

        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(15)
            pending = terminal('client-connection-status', [binary, '--no-check-update', 'client',
                               f'127.0.0.1:{listener.getsockname()[1]}', '--test-pattern', '--audio=false', '--splash-screen=true', '--utf8', 'true'])
            peer, _ = listener.accept()
            status = '[⠋⠙⠹⠸⠼⠴⠦⠧]' + re.escape(' ascii-chat • client • connecting...')
            try:
                title_pattern(pending, status, 'No connecting spinner')
                pending.write(']')
                title_pattern(pending, status + re.escape(' • log viewer'), 'Connecting status lost under log viewer')
                pending.write('?')
                title_pattern(pending, status + re.escape(' • log viewer • help'), 'Connecting status lost under help')
                peer.close()
                retry = '[⠋⠙⠹⠸⠼⠴⠦⠧]' + re.escape(' ascii-chat • client • reconnecting...')
                title_pattern(pending, retry + re.escape(' • log viewer • help'), 'Reconnecting status lost under help', timeout=20)
            finally:
                peer.close()
                pending.interrupt()
                pending.close()
        print('PASS connecting/reconnecting spinner remains before log viewer and help breadcrumbs')

        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            listener.settimeout(15)
            discovery = terminal('discovery-status', [binary, '--no-check-update',
                                 '--discovery-service', '127.0.0.1',
                                 '--discovery-service-port', str(listener.getsockname()[1]),
                                 '--test-pattern', '--audio=false', '--splash-screen=true', '--utf8', 'true'])
            peer, _ = listener.accept()
            try:
                connecting = '[⠋⠙⠹⠸⠼⠴⠦⠧]' + re.escape(' ascii-chat • discovery • connecting...')
                title_pattern(discovery, connecting, 'Discovery did not animate while connecting')
                discovery.write(']')
                title_pattern(discovery, connecting + re.escape(' • log viewer'), 'Discovery status lost under log viewer')
                discovery.write('?')
                title_pattern(discovery, connecting + re.escape(' • log viewer • help'), 'Discovery status lost under help')
            finally:
                peer.close()
                discovery.interrupt()
                discovery.close()
        print('PASS discovery connection spinner remains before TUI/help breadcrumbs')

        for unicode in (True, False):
            destination = args.output.resolve() / ('unicode.mp4' if unicode else 'ascii.mp4')
            term = terminal('render-unicode' if unicode else 'render-ascii', mirror(unicode) + ['--render-file', str(destination)])
            suffix = ' ascii-chat • mirror • rendering file...'
            if unicode:
                title_pattern(term, '[⠋⠙⠹⠸⠼⠴⠦⠧]' + re.escape(suffix), 'Rendering spinner did not appear')
                term.pump(0.7)
                spinners = {title.removesuffix(suffix) for _, title in term.titles if title.endswith(suffix)}
                assert len(spinners) >= 2, f'Spinner did not animate: {spinners}'
            else:
                term.expect_title('')
                term.pump(2)
                assert {title for _, title in term.titles} == {''}, term.titles
            term.interrupt()
            term.close()
            assert destination.stat().st_size > 1000, 'No encoded video produced'
            print(f'PASS {"Unicode rendering spinner" if unicode else "empty rendering title without UTF-8"}')

        kitty = terminal('render-kitty', mirror(True) + ['--width', '20', '--height', '10', '--snapshot', '--snapshot-delay', '2', '--render-kitty'],
                         environment={'TERM': 'xterm-kitty'})
        title_pattern(kitty, '[⠋⠙⠹⠸⠼⠴⠦⠧]' + re.escape(' ascii-chat • mirror • rendering file...'),
                      'Kitty image rendering did not animate the title', timeout=20)
        kitty.expect(lambda _: re.search(r'\x1b_G[^;]*m=0;[A-Za-z0-9+/=]*\x1b\\', ''.join(kitty.raw)) is not None,
                     'No complete Kitty image was emitted', timeout=25)
        packets = re.findall(r'\x1b_G.*?\x1b\\', ''.join(kitty.raw), re.S)
        reconstruct(''.join(packets).encode('utf-8'))
        kitty.close()
        print('PASS Kitty image spinner and independently reconstructed protocol packets')

        result = subprocess.run(mirror(False) + ['--snapshot', '--snapshot-delay', '0'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert result.returncode == 0, result.stderr.decode(errors='replace')[-2000:]
        assert result.stdout.strip(), 'No snapshot output'
        assert b'\x1b]0;' not in result.stdout + result.stderr
        assert b'\x1b]2;' not in result.stdout + result.stderr
        short = subprocess.run([binary, '--help'], capture_output=True, timeout=5)
        assert short.returncode == 0
        assert b'\x1b]0;' not in short.stdout + short.stderr
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
