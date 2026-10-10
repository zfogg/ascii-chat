"""Verify keyboard-controlled audio visualizations through a real terminal.

Requires ffmpeg and the terminal_ui.py dependencies, plus an audio output device.
Run: python tests/integration/audio_visualization_keys.py --binary build/bin/ascii-chat.exe
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
from terminal_ui import Terminal


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = str(args.binary.resolve())
    with tempfile.TemporaryDirectory(prefix='ascii-audio-keys-') as directory:
        root = Path(directory)
        media = root / 'signal.mp4'
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=160x120:rate=15', '-f', 'lavfi', '-i',
                        'aevalsrc=0.5*sin(2*PI*(220+180*t)*t)*(0.6+0.4*sin(2*PI*2*t)):s=48000',
                        '-t', '12', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-c:a', 'aac', str(media)], check=True)
        term = Terminal([binary, '--no-check-update', '--log-file',
                         str(root/'audio-keys-runtime.log'), 'mirror', '--file', str(media),
                         '--loop', '--audio-source', 'media', '--volume', '0', '--splash-screen=false',
                         '--color-mode', 'truecolor'], rows=24, cols=100)
        try:
            term.pump(5)
            term.write('?')
            term.expect(lambda s: '(2) Audio Waveform : X' in s and '(3) Audio Frequencies (FFT) : X' in s, 'Help lists audio controls')
            help_text = term.pump(.2)
            heading = next(line for line in help_text.splitlines() if 'Navigation & Control:' in line)
            assert 'Current Settings:' in heading, help_text
            assert heading.index('Current Settings:') > heading.index('Navigation & Control:'), help_text
            term.write('1')
            term.expect(lambda s: '(1) Matrix \"Digital Rain\" : O' in s, 'Matrix enables')
            term.write('2')
            term.expect(lambda s: '(2) Audio Waveform : O' in s and '(1) Matrix \"Digital Rain\" : X' in s, 'Waveform disables Matrix')
            term.write('?')
            wave = term.expect(lambda s: set(s.strip()) <= set(' #+\n') and s.count('#') > 100, 'Non-silent waveform')
            before = term.pump(.5)
            assert term.pump(.7) != before, 'Waveform did not update'
            term.write('?')
            term.expect(lambda s: 'Keyboard Shortcuts' in s, 'Help reopens')
            term.write('1')
            term.expect(lambda s: '(1) Matrix \"Digital Rain\" : O' in s, 'Matrix re-enables')
            term.expect(lambda s: '(2) Audio Waveform : X' in s and '(3) Audio Frequencies (FFT) : X' in s, 'Matrix disables waveform')
            term.write('3')
            term.expect(lambda s: '(3) Audio Frequencies (FFT) : O' in s and '(1) Matrix \"Digital Rain\" : X' in s, 'FFT disables Matrix')
            term.write('?')
            fft = term.expect(lambda s: set(s.strip()) <= set(' .:-=+*#%@\n') and len(s.strip()) > 100
                              and any(c in s for c in ':%@'), 'Non-silent frequency display')
            before = term.pump(.5)
            assert term.pump(.7) != before, 'FFT did not update'
            term.write('?')
            term.expect(lambda s: '(2) Audio Waveform : X' in s and '(3) Audio Frequencies (FFT) : O' in s, 'Mutually exclusive status')
            term.write('1')
            term.expect(lambda s: '(3) Audio Frequencies (FFT) : X' in s and '(1) Matrix \"Digital Rain\" : O' in s, 'Matrix disables FFT')
            term.write('3')
            term.expect(lambda s: '(3) Audio Frequencies (FFT) : O' in s, 'FFT re-enables')
            term.write('3')
            term.expect(lambda s: '(3) Audio Frequencies (FFT) : X' in s, 'FFT toggles off')
            term.write('?')
            term.expect(lambda s: any(c.isalpha() for c in s) and 'Keyboard Shortcuts' not in s, 'Video restored')
            term.write('2')
            term.expect(lambda s: set(s.strip()) <= set(' #+\n') and s.count('#') > 100, 'Waveform re-enables')
            term.write('2')
            term.expect(lambda s: any(c.isalpha() for c in s) and 'Keyboard Shortcuts' not in s, 'Waveform toggles off')
            term.interrupt()
            print('PASS waveform/FFT keyboard activation, live audio rendering, mutual exclusion, disable and video restoration')
        finally:
            (root/'audio-keys-terminal.txt').write_text('\n'.join(term.screen.display), encoding='utf-8')
            term.close()

if __name__ == '__main__':
    main()
