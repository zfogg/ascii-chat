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
    parser.add_argument('--artifacts', type=Path)
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
                                 '--color-mode', mode], rows=36, cols=110)
                try:
                    term.pump(2)
                    term.write('?')
                    term.expect(lambda text: 'Press ? to close' in text, 'Missing help footer')
                    key = 'f0cc91' if mode == 'truecolor' else 'default'
                    number = 'c8a0d8' if mode == 'truecolor' else 'default'
                    for label, span, color in (
                        ('m / M', 'm', key), ('m / M', 'M', key), ('m / M', '/', 'default'),
                        ('Close overlay / Quit app', '/', 'default'), ('Esc    Close', 'Esc', key),
                        ('(1) Matrix', '1', number), ('(1) Matrix', '(', 'default'),
                        ('(z) Zalgo', 'z', key), ('Volume :', '100', number),
                        ('Volume up/down (10%)', '10', number), ('Press ? to close', '?', key),
                        ('-      FPS counter', '-', key), ('=      Running app statistics TUI', '=', key),
                        ('0      C code sync primitives TUI', '0', number),
                        ('FPS counter', 'FPS counter', 'default'),
                        ('Running app statistics TUI', 'Running app statistics TUI', 'default'),
                    ):
                        # Volume is a value after the label, rather than part of it.
                        if label == 'Volume :':
                            label = next(line.strip() for line in term.screen.display if label in line)
                        check_span(term, label, span, color)
                    descriptions = ('Toggle this help screen TUI', 'Live log viewer/searcher TUI',
                                    'Close overlay / Quit app', 'TUI to open media from path or url',
                                    'Open from clipboard', 'Queue dropped/pasted media',
                                    'Previous/next media (restart)', 'Mute/Unmute audio',
                                    'Volume up/down (10%)', 'Cycle color mode', 'Cycle color filter',
                                    'Flip webcam horizontally', 'Flip webcam vertically', 'Cycle render mode',
                                    'FPS counter', 'Running app statistics TUI', 'Print current sync primitive state',
                                    'C code sync primitives TUI')
                    positions = [next((row, line.index(label)) for row, line in enumerate(term.screen.display)
                                      if label in line) for label in descriptions]
                    assert len({col for _, col in positions}) == 1, positions
                    fps_row = next(row for row, line in enumerate(term.screen.display) if 'FPS counter' in line)
                    group = ('TUI to open media from path or url', 'Live log viewer/searcher TUI',
                             'Running app statistics TUI', 'C code sync primitives TUI')
                    group_rows = [next(row for row, line in enumerate(term.screen.display) if label in line)
                                  for label in group]
                    assert group_rows == list(range(group_rows[0], group_rows[0] + len(group))), group_rows
                    assert group_rows[0] > fps_row
                    assert '(0)' not in '\n'.join(term.screen.display)
                    assert '=' not in term.screen.display[fps_row], term.screen.display[fps_row]
                    # The longest binding occupies five cells, followed by two spaces.
                    check_span(term, '< / >  Previous', '  ', 'default')
                    for label in ('Current Settings:', 'Animations (number key toggle):'):
                        row = next(line for line in term.screen.display if label in line)
                        assert row.index(label) == row.index('║') + 3 + max(
                            7 + len(description) for description in descriptions) + 2, row
                    if args.artifacts and mode == 'truecolor':
                        from sync_tui_modes import capture
                        capture(term, args.artifacts, 'keyboard-help-aligned')
                        from PIL import Image
                        import time
                        frames, timestamps = [], []
                        for index in range(12):
                            term.pump(0.08)
                            timestamps.append(time.monotonic())
                            name = f'keyboard-help-frame-{index:02d}'
                            capture(term, args.artifacts, name)
                            with Image.open(args.artifacts / f'{name}.png') as image:
                                frames.append(image.copy())
                        durations = [round((later - earlier) * 1000)
                                     for earlier, later in zip(timestamps, timestamps[1:])]
                        frames[0].save(args.artifacts / 'keyboard-help-animated.gif', save_all=True,
                                       append_images=frames[1:], duration=durations + durations[-1:], loop=0)
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
