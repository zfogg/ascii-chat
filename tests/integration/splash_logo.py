"""Verify the native splash retains its CRLF-terminated rainbow logo rows."""
import argparse
from pathlib import Path
import socket
import threading

from terminal_ui import Terminal
from sync_tui_modes import capture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    stop = threading.Event()
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        listener.settimeout(10)
        def hold_handshake():
            try:
                connection, _ = listener.accept()
                with connection:
                    stop.wait(10)
            except OSError:
                pass
        worker = threading.Thread(target=hold_handshake, daemon=True)
        worker.start()
        term = Terminal([str(args.binary.resolve()), '--no-check-update', '--log-file', str(args.artifacts / 'splash.log'),
                         'client', f'127.0.0.1:{listener.getsockname()[1]}',
                         '--test-pattern', '--audio=false', '--splash-screen=true', '--color-mode', 'truecolor'],
                        rows=40, cols=110)
        try:
            term.expect(lambda text: 'Video chat in your terminal' in text and
                        sum('__' in line for line in text.splitlines()) >= 3, 'Splash logo rows were erased', timeout=8)
            capture(term, args.artifacts, 'splash-rainbow-logo')
        finally:
            stop.set()
            term.close()
            worker.join(2)
    print('PASS: native splash rainbow logo rows survive CRLF composition')


if __name__ == '__main__':
    main()
