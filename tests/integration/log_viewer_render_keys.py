"""Smoke test log-viewer shortcut isolation through a real terminal."""
import argparse
import os
from pathlib import Path
import tempfile

from terminal_ui import Terminal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    previous = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='ascii-log-keys-') as directory:
        root = Path(directory)
        for key in list(os.environ):
            if key.startswith('ASCII_CHAT_'):
                del os.environ[key]
        os.environ.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root))
        term = None
        try:
            term = Terminal([str(args.binary.resolve()), '--no-check-update', '--log-file', str(root / 'app.log'),
                             'mirror', '--test-pattern', '1', '--audio=false', '--splash-screen=false'],
                            rows=40, cols=140)
            term.pump(2)

            def settings():
                term.write('?')
                text = term.expect(lambda t: 'Current Settings:' in t, 'Renderer help did not open')
                labels = ('Color  :', 'Filter :', 'Render :', 'Flip   :', 'Face Follow :',
                          '(1) Matrix', '(2)', '(3)', '(z) Zalgo', '(-) FPS Counter')
                values = [line[line.index(label):].strip() for line in text.splitlines()
                          for label in labels if label in line]
                assert len(values) >= 6, values
                term.write('?')
                term.expect(lambda t: 'Current Settings:' not in t, 'Renderer help did not close')
                return values

            before = settings()
            for key in 'cCrRfFxXyY123wWzZ-':
                term.write(']')
                term.expect(lambda t: '/: search logs' in t, 'Log viewer did not open')
                term.write(key)
                term.pump(0.08)
                term.write(']')
                term.expect(lambda t: '/: search logs' not in t, 'Log viewer did not close')
                assert settings() == before, f'Rendering settings changed for log key {key!r}'
            # The same shortcut must still work when the renderer owns input.
            term.write('x')
            term.pump(0.2)
            assert settings() != before, 'Renderer shortcut stopped working outside logs'

            for key, marker in (('o', 'Open media:'), ('=', 'LIVE STATS'), ('0', 'Sync primitives')):
                term.write(']')
                term.expect(lambda t: '/: search logs' in t, 'Log viewer did not reopen')
                term.write(key)
                term.expect(lambda t: marker in t and '/: search logs' not in t,
                            f'TUI shortcut {key!r} did not switch from logs')
                term.write('\x1b')
                term.expect(lambda t: marker not in t, f'TUI {key!r} did not close')
            print('PASS: log viewer isolates rendering keys; renderer and TUI shortcuts work')
        finally:
            if term:
                term.close()
            os.environ.clear()
            os.environ.update(previous)


if __name__ == '__main__':
    main()
