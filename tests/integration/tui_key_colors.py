"""Check key, number, and separator colors in the help and sync screens."""
import argparse
import os
from pathlib import Path
import tempfile

from terminal_ui import Terminal


def check_span(term, label, span, color):
    row = next(i for i, text in enumerate(term.screen.display) if label in text)
    col = term.screen.display[row].index(label) + label.index(span)
    actual = [term.screen.buffer[row][col + i].fg for i in range(len(span))]
    assert actual == [color] * len(span), (label, span, actual, color)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    previous = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='ascii-key-colors-') as directory:
        root = Path(directory)
        for key in list(os.environ):
            if key.startswith('ASCII_CHAT_'):
                del os.environ[key]
        os.environ.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root))
        try:
            for mode in ('truecolor', 'none'):
                term = Terminal([str(args.binary.resolve()), '--no-check-update', '--log-file', str(root / 'app.log'),
                                 'mirror', '--test-pattern', '1', '--audio=false', '--splash-screen=false',
                                 '--color-mode', mode], rows=55, cols=180)
                try:
                    term.pump(2)
                    term.write('?')
                    term.expect(lambda text: 'Press ? to close' in text, 'Missing help footer')
                    key = 'f0cc91' if mode == 'truecolor' else 'default'
                    number = 'c8a0d8' if mode == 'truecolor' else 'default'
                    for label, span, color in (
                        ('m / M', 'm', key), ('m / M', 'M', key), ('m / M', '/', 'default'),
                        ('Close overlay / Quit app', '/', 'default'), ('Esc     Close', 'Esc', key),
                        ('(1) Matrix', '1', number), ('(1) Matrix', '(', 'default'),
                        ('(z) Zalgo', 'z', key), ('Volume :', '100', number),
                        ('Volume up/down (10%)', '10', number), ('Press ? to close', '?', key),
                        ('- FPS counter', '-', key), ('= Live statistics', '=', key),
                    ):
                        # Volume is a value after the label, rather than part of it.
                        if label == 'Volume :':
                            label = next(line.strip() for line in term.screen.display if label in line)
                        check_span(term, label, span, color)
                    term.write('0')
                    term.expect(lambda text: 'Home/End' in text and 'Keyboard Shortcuts' not in text,
                                'Missing sync footer')
                    for label in ('0/Esc', 'Left/Right', 'Up/Down', 'Home/End'):
                        first, second = label.split('/')
                        check_span(term, label, first, number if first == '0' else key)
                        check_span(term, label, second, key)
                        check_span(term, label, '/', 'default')
                    print(f'PASS {mode}: help and sync key/number/separator colors', flush=True)
                finally:
                    term.close()
        finally:
            os.environ.clear()
            os.environ.update(previous)


if __name__ == '__main__':
    main()
