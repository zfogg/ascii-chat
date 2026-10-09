import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path('tests/integration').resolve()))
from terminal_ui import Terminal
root = Path.cwd()
cmd = [sys.executable, str(root/'tests/integration/prompt_timeouts.py'), '--library', str(root/'build/bin/asciichat.dll'), '--probe', 'mdns', '--log', str(root/'artifacts/timed-mdns.log')]
term = Terminal(cmd, rows=40, cols=110)
try:
    term.expect(lambda text: 'Select server' in text, 'mDNS prompt missing', timeout=10)
    text = term.expect(lambda text: 'PASS mdns' in text, 'mDNS did not time out', timeout=35)
    match = re.search(r'PASS mdns ([0-9.]+)s', text)
    assert match, text
    assert 29.9 <= float(match[1]) < 32, match[1]
    raw = ''.join(term.raw)
    assert 'Prompt timed out after 30 seconds' in raw
    (root/'artifacts/timed-mdns-terminal.txt').write_text(text, encoding='utf-8')
    print(f'mDNS selection: {match[1]} seconds measured inside the native probe; returned cancellation; stderr timeout diagnostic verified.')
finally:
    term.close()
