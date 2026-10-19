from pathlib import Path
import json, re, hashlib, shutil

root = Path('.tmp/verified-captures')
screens = Path('.tmp/verified-screenshots')
reports = json.loads((screens / 'screenshots.json').read_text(encoding='utf8'))
count = 0
for directory in root.glob('*-frames'):
    manifest = json.loads((directory / 'manifest.json').read_text(encoding='utf8'))
    assert manifest['source_sha'] == 'a9ad621303cc8140979b78a4b5ce4ad25c6df22d'
    assert manifest['runtime_libraries']
    for case in manifest['cases']:
        count += 1
        assert case['exit'] == 0 and not case['timeout']
        raw = (directory / (case['name'] + '.ansi')).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == case['frame_sha256']
        log = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '',
                     (directory / (case['name'] + '.stderr')).read_text(encoding='utf8'))
        assert not re.search(r'\b(?:ERROR|FATAL)\b|ERROR: AddressSanitizer|runtime error:', log), case['name']
for group in reports:
    for case in group['cases']:
        assert case['exit'] == 0 and case['ink'] > 4000 and case['replacement'] == 0, case
        colored = not case['name'].endswith('-mono')
        needs_bg = colored and ('-background-' in case['name'] or '-half-block-' in case['name'])
        assert (case['bgPainted'] > 0) == needs_bg, case
    for case in json.loads((screens / (group['id'] + '.cells.json')).read_text(encoding='utf8')):
        assert len([line for line in case['lines'] if line.strip()]) == 40, case['name']
        assert max(map(len, case['lines'])) <= 120, case['name']
assert count == 240 and len(reports) == 20
print('PASS: 240 exits, source/library provenance, hashes, 120x40 geometry, backgrounds, UTF-8 and error scan')
evidence = Path('.tmp/evidence')
(evidence / 'captures').mkdir(exist_ok=True)
(evidence / 'screenshots').mkdir(exist_ok=True)
for directory in root.glob('*-frames'):
    shutil.copytree(directory, evidence / 'captures' / directory.name, dirs_exist_ok=True)
for file in screens.glob('*'):
    shutil.copy2(file, evidence / 'screenshots' / file.name)
for name in ['capture_matrix.py', 'extract_frames.py', 'screenshots.cjs', 'backend_probe.c', 'validate_evidence.py']:
    shutil.copy2(Path('.tmp') / name, evidence / name)
(evidence / 'validation.json').write_text(json.dumps({
    'source_sha': manifest['source_sha'], 'cases': count, 'screenshots': len(reports),
    'exit_failures': 0, 'renderer_errors': 0, 'replacement_characters': 0, 'geometry': '120x40',
    'visual_review': 'All 20 matrices reviewed: gradient/square, rainbow/circle, readable test label, expected color modes and half blocks.'
}, indent=2), encoding='utf8')
