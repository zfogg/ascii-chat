"""Check the stats screen's semantic colors in real terminal cells."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from terminal_ui import Terminal
from sync_tui_modes import capture


def check_span(term, label, span, expected):
    row = next(i for i, line in enumerate(term.screen.display) if label in line)
    line = term.screen.display[row]
    col = line.index(span, line.index(label))
    actual = [term.screen.buffer[row][col + i].fg for i in range(len(span))]
    assert actual == [expected] * len(span), (line, span, actual, expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    previous = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='ascii-stats-colors-') as directory:
        root = Path(directory)
        for key in list(os.environ):
            if key.startswith('ASCII_CHAT_'):
                del os.environ[key]
        os.environ.update(HOME=str(root), USERPROFILE=str(root), APPDATA=str(root), XDG_CONFIG_HOME=str(root))
        media = root / 'sample.mkv'
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'color=size=160x90:rate=15',
                        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000', '-t', '5',
                        '-c:v', 'libx264', '-preset', 'ultrafast', '-c:a', 'pcm_f32le', str(media)], check=True)
        try:
            for mode, audio in [('truecolor', True), ('truecolor', False), ('none', False)]:
                name = f'stats-{mode}-audio-{audio}'
                term = Terminal([str(args.binary.resolve()), '--no-check-update', '--log-file', str(root / (name + '.log')),
                                 'mirror', '--file', str(media), '--loop', '--audio=' + str(audio).lower(),
                                 '--volume', '0', '--microphone-volume', '0', '--splash-screen=false',
                                 '--color-mode', mode], rows=40, cols=104)
                try:
                    term.pump(2)
                    term.write('=')
                    term.expect(lambda text: 'Audio:' in text and 'LIVE STATS' in text, 'Stats view did not open')
                    term.pump(.4)
                    blue, purple, red, green = ('65ace1', 'c8a0d8', 'e85d6f', '90e070') if mode == 'truecolor' else ('default',) * 4
                    for label in ('ASCII-CHAT', 'OVERVIEW', 'PIPELINE / NETWORK', 'TOTAL', 'PER SECOND', 'TIMINGS', 'LIFETIME MAX'):
                        check_span(term, label, label, blue)
                    for row, line in enumerate(term.screen.display):
                        for match in re.finditer(r'\d+(?:\.\d+)?', line):
                            actual = [term.screen.buffer[row][i].fg for i in range(*match.span())]
                            assert actual == [purple] * len(match[0]), (line, match[0], actual)
                    check_span(term, 'Audio:', 'enabled' if audio else 'disabled', green if audio else red)
                    check_span(term, 'Recording:', 'disabled', red)
                    if audio:
                        gauge = next(line.strip() for line in term.screen.display if 'audio_buffered_samples:' in line)
                        assert re.fullmatch(r'audio_buffered_samples: \d+', gauge), gauge
                    capture(term, args.artifacts, name)
                    term.write('\t')
                    term.expect(lambda text: 'CONNECTIONS & MODE DETAILS' in text, 'Stats details did not open')
                    term.pump(.3)
                    check_span(term, 'TRANSPORT', 'TRANSPORT', blue)
                    header = next(i for i, line in enumerate(term.screen.display) if 'TRANSPORT' in line)
                    col = term.screen.display[header].index('TRANSPORT')
                    assert term.screen.buffer[header][col].reverse, 'Selected row lost its highlight'
                    capture(term, args.artifacts, name + '-details')
                finally:
                    term.close()
        finally:
            os.environ.clear()
            os.environ.update(previous)
    print('PASS: blue headers, purple numbers, red disabled, green enabled; plain mode and selected rows preserved')


if __name__ == '__main__':
    main()
