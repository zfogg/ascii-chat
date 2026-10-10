"""Render-file UI and output regression tests using a real PTY and FFmpeg.

Requires the terminal_ui.py dependencies (pyte and pywinpty/pexpect), ffmpeg,
and ffprobe. --font-size controls real encoder load for the backlog test;
raise it on machines that finish encoding before playback ends.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
from wcwidth import wcswidth

from terminal_ui import Terminal


def run(*args, **kwargs):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)


def verify(path):
    metadata = json.loads(run('ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(path)).stdout)
    video = next(s for s in metadata['streams'] if s['codec_type'] == 'video')
    assert int(video['width']) > 0 and int(video['height']) > 0, metadata
    run('ffmpeg', '-v', 'error', '-i', str(path), '-f', 'null', '-')
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--font-size', default='48')
    args = parser.parse_args()
    root = args.artifacts.resolve()
    root.mkdir(parents=True, exist_ok=True)
    binary = str(args.binary.resolve())
    media = root / 'source.mp4'
    run('ffmpeg', '-y', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=160x120:rate=30',
        '-t', '3', '-c:v', 'mpeg4', str(media))
    base = [binary, '--no-check-update', '--log-level', 'warn', '--log-file', str(root / 'app.log'),
            'mirror', '--file', str(media), '--audio=false', '--splash-screen=false', '--fps', '30']
    results = {}
    # Named output retains ASCII on redirected stdout. Progress must not leak into text output.
    named = root / 'redirected.mp4'
    result = run(*base, '--snapshot', '--snapshot-delay', '1', '--width', '40', '--height', '12',
                 '--color-mode', 'none', '--strip-ansi', '--render-file', str(named), timeout=60)
    assert result.stdout.strip(), 'Named recording suppressed redirected ASCII'
    assert b'Rendering file' not in result.stdout and b'Finalizing file' not in result.stdout
    assert b'\x1b' not in result.stdout, 'ANSI leaked into stripped output'
    results['redirected'] = verify(named)
    (root / 'redirected.txt').write_bytes(result.stdout)
    # Binary stdout must remain a decodable video stream without ASCII or progress text.
    result = run(*base, '--snapshot', '--snapshot-delay', '1', '--width', '40', '--height', '12',
                 '--render-file', '-', timeout=60)
    piped = root / 'stdout.video'
    piped.write_bytes(result.stdout)
    results['binary_stdout'] = verify(piped)
    # Exercise actual queued work. Large glyphs slow pixel rendering and encoding, not capture.
    output = root / 'progress.mp4'
    term = Terminal(base + ['--snapshot', '--snapshot-delay', '2', '--width', '120', '--height', '36',
                           '--render-font-size', args.font_size, '--render-file', str(output)], cols=120, rows=40)
    states, labels, previews = [], [], {}
    resized = False
    saw_resized_progress = False
    try:
        deadline = time.monotonic() + 150
        while term.process.isalive() and time.monotonic() < deadline:
            text = term.pump(.08)
            states.append(text)
            match = re.search(r'([|/\\-]) Rendering file \((\d+):(\d+)\) \(frame (\d+)/(\d+)\)', text)
            if match:
                labels.append(match.groups())
                rows = text.splitlines()
                progress_row = next(i for i, row in enumerate(rows) if 'Rendering file' in row)
                assert '--render-file Progress' in rows[progress_row - 1], 'Progress title missing'
                destination = str(output)
                if wcswidth(destination) > term.screen.columns - 4:
                    while wcswidth(destination) > term.screen.columns - 5:
                        destination = destination[:-1]
                    destination += '…'
                assert destination in rows[progress_row + 1], 'Output path missing below progress'
                previews.setdefault(term.screen.columns, set()).add('\n'.join(line for line in text.splitlines() if 'Rendering file' not in line))
                if resized:
                    saw_resized_progress = True
                    (root / 'resized.txt').write_text(text, encoding='utf-8')
                if not resized:
                    (root / 'draining.txt').write_text(text, encoding='utf-8')
                    term.resize(24, 60)
                    resized = True
        assert not term.process.isalive(), 'Recording did not finish and join workers'
        term.pump(.2)
        status = term.process.exitstatus
        assert status == 0, f'Recording exited {status}'
        assert labels, 'No draining UI observed; increase --font-size to create an encoding backlog'
        assert len({x[0] for x in labels}) > 1, 'Spinner did not animate'
        counts = [int(x[3]) for x in labels]
        assert counts == sorted(counts) and max(counts) > min(counts), counts
        assert len({x[1:3] for x in labels}) > 1, 'Elapsed timer did not advance'
        assert {int(x[4]) for x in labels} == {60}, labels
        assert any(len(frames) > 1 for frames in previews.values()), 'Latest-frame background did not change'
        assert saw_resized_progress, 'Resize lost progress UI'
        assert 'Finalizing file' in ''.join(term.raw), 'Recording closed without presenting finalization'
        results['progress'] = dict(samples=len(labels), first=int(labels[0][3]), last=int(labels[-1][3]),
                                   total=60, spinner_states=sorted({x[0] for x in labels}), resized=resized)
    finally:
        (root / 'progress.ansi').write_text(''.join(term.raw), encoding='utf-8')
        (root / 'states.json').write_text(json.dumps(states, ensure_ascii=False), encoding='utf-8')
        term.close()
    results['progress_video'] = verify(output)
    # A graceful stop must still display progress after the global shutdown flag is set.
    interrupted = root / 'interrupted.mp4'
    term = Terminal(base + ['--width', '120', '--height', '36', '--render-font-size', args.font_size,
                            '--render-file', str(interrupted)], cols=120, rows=40)
    try:
        term.expect(lambda text: any(len(row.strip()) > 30 and re.fullmatch(r"[ .,:;clodxkO0KXNWM@]+", row)
                                    for row in text.splitlines()), 'No live ASCII before interrupt', timeout=30)
        term.pump(1)
        term.write('\x03')
        stop_states = []
        deadline = time.monotonic() + 90
        while term.process.isalive() and time.monotonic() < deadline:
            stop_states.append(term.pump(.1))
        term.pump(.2)
        assert not term.process.isalive(), 'Interrupted recording did not drain'
        assert term.process.exitstatus == 0, term.process.exitstatus
        assert any('Rendering file' in text for text in stop_states), 'No progress during graceful shutdown'
        results['graceful_stop'] = verify(interrupted)
    finally:
        (root / 'interrupted.ansi').write_text(''.join(term.raw), encoding='utf-8')
        term.close()
    # Fail after encoding starts, exercising delayed write/flush errors as well as cleanup.
    process = subprocess.Popen(base + ['--snapshot', '--snapshot-delay', '2', '--width', '40', '--height', '12',
                                       '--render-file', '-'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    try:
        assert process.stdout.read(4096), 'No binary data before closing consumer'
        process.stdout.close()
        status = process.wait(timeout=30)
        assert status != 0, 'Closed output pipe returned success'
        results['closed_pipe_exit'] = status
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    # Initialization failure must return nonzero rather than display a successful export.
    failure = subprocess.run(base + ['--snapshot', '--render-file', str(root / 'missing' / 'failed.mp4')],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    assert failure.returncode != 0, 'Recording creation failure returned success'
    results['failure_exit'] = failure.returncode
    (root / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    print('PASS: redirected ASCII, binary stdout, animated drain, resize, graceful stop, decoded video, failure exits')


if __name__ == '__main__':
    main()
