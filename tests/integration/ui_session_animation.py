"""Exercise the shared native/browser compositor against deterministic timestamps.

Run with --library build/bin/asciichat.dll (or the native shared library).
Requires pyte, as used by terminal_ui.py.
"""
import argparse
import ctypes as c
import os
from pathlib import Path

from terminal_ui import CaptureScreen, CaptureStream


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    args = parser.parse_args()
    library = args.library.resolve()
    dll_directory = os.add_dll_directory(str(library.parent)) if os.name == 'nt' else None
    lib = c.CDLL(str(library))

    def bind(name, result, *arguments):
        function = getattr(lib, name)
        function.restype, function.argtypes = result, arguments
        return function

    initialize = bind('options_state_init', c.c_int)
    destroy_options = bind('options_state_destroy', None)
    set_int = bind('options_set_int', c.c_int, c.c_char_p, c.c_int)
    create = bind('ui_session_create', c.c_void_p)
    destroy = bind('ui_session_destroy', None, c.c_void_p)
    resize = bind('ui_session_resize', None, c.c_void_p, c.c_int, c.c_int)
    key = bind('ui_session_input', c.c_int, c.c_void_p, c.c_int)
    tick = bind('ui_session_tick', c.c_char_p, c.c_void_p, c.c_uint64)
    deadline = bind('ui_session_next_frame_ns', c.c_uint64, c.c_void_p)
    assert initialize() == 0
    assert set_int(b'color_mode', 3) == 0
    session = create()
    assert session
    try:
        resize(session, 110, 48)
        key(session, ord('?'))
        now = 1_000_000_000
        frame = tick(session, now)
        assert frame and b'Keyboard Shortcuts' in frame
        screen = CaptureScreen(110, 48)
        stream = CaptureStream(screen)
        stream.feed(frame.decode())
        baseline = screen.display
        occupied = [row for row, line in enumerate(baseline) if line.strip()]
        assert abs(occupied[0] - (47 - occupied[-1])) <= 1, 'Combined layout is not vertically centered'
        box_top = next(row for row, line in enumerate(baseline) if '╔' in line)
        assert box_top == occupied[0] + 5, 'Logo must have four rows and one blank row above the box'
        box_line = baseline[box_top]
        assert abs(box_line.index('╔') - (109 - box_line.index('╗'))) <= 1
        logo_left = min(len(baseline[row]) - len(baseline[row].lstrip()) for row in occupied[:4])
        logo_right = max(len(baseline[row].rstrip()) - 1 for row in occupied[:4])
        assert abs(logo_left + logo_right - box_line.index('╔') - box_line.index('╗')) <= 1
        first_colors = [screen.buffer[y][x].fg for y in range(48) for x in range(110)]
        for index in range(1, 61):
            following = deadline(session)
            assert 0 < following - now <= 16_666_667, (index, now, following)
            assert tick(session, following - 1000) is None, 'Redrew before animation deadline'
            frame = tick(session, following)
            assert frame and b'Keyboard Shortcuts' in frame
            assert b'\x1b[2J' not in frame and b'\x1b[J' not in frame, 'Cleared an animated frame'
            stream.feed(frame.decode())
            assert screen.display == baseline, 'Animated frame moved or erased logo/help text'
            now = following
        assert abs(now - 2_000_000_000) < 10, now
        last_colors = [screen.buffer[y][x].fg for y in range(48) for x in range(110)]
        assert first_colors != last_colors, 'Rainbow colors did not advance'
        assert first_colors[box_top * 110:] == last_colors[box_top * 110:], 'Help box colors changed during animation'
        print('PASS shared compositor: exactly 60 animated frames/second; text/layout stable; no clears')

        resize(session, 110, 10)
        now += 1
        assert b'Terminal' in tick(session, now)
        assert deadline(session) == 2**64 - 1
        now += 5_000_000_000
        assert tick(session, now) is None, 'Covered animation continued redrawing'
        resize(session, 110, 48)
        frame = tick(session, now)
        stream.feed(frame.decode())
        assert screen.display == baseline
        assert [screen.buffer[y][x].fg for y in range(48) for x in range(110)] == last_colors
        key(session, ord('?'))
        tick(session, now + 1)
        assert deadline(session) == 2**64 - 1
        assert tick(session, now + 1_000_000_000) is None
        print('PASS shared compositor: too-small pauses animation; resize resumes; closing help stops redraws')
        key(session, ord('?'))
        now += 2_000_000_000
        resize(session, 110, 48)

        def face_status():
            nonlocal now
            now += 20_000_000
            stream.feed(tick(session, now).decode())
            return next(line.split('Face Follow :', 1)[1].strip().split()[0]
                        for line in screen.display if 'Face Follow :' in line)

        assert face_status() == 'X'
        key(session, ord('w'))
        assert face_status() == 'O'
        key(session, ord('4'))
        assert face_status() == 'O', 'Removed 4 binding still toggles Face Follow'
        key(session, ord('W'))
        assert face_status() == 'X'
        assert any('w / W  Toggle Face Follow' in line for line in screen.display)
        assert not any('(4) Face' in line for line in screen.display)
        print('PASS shared input: w/W toggle Face Follow; 4 has no effect; setting is outside animations')
    finally:
        destroy(session)
        destroy_options()
        if dll_directory:
            dll_directory.close()


if __name__ == '__main__':
    main()
