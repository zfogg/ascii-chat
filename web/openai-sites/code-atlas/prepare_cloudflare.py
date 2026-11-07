"""Package the static atlas for Cloudflare's per-asset size limit."""
from pathlib import Path
import gzip
import re
import shutil

base = Path(__file__).resolve().parent
source = base / 'dist'
destination = base / 'cloudflare-dist'
destination.mkdir(exist_ok=True)
for name in ('index.html', 'style.css', 'guides.js'):
    shutil.copyfile(source / name, destination / name)
raw = (source / 'data.json').read_bytes()
compressed = gzip.compress(raw, compresslevel=9, mtime=0)
assert gzip.decompress(compressed) == raw
(destination / 'source-data.bin').write_bytes(compressed)
script = (source / 'app.js').read_text(encoding='utf8')
script, replacements = re.subn(r'''await\s*\(\s*await\s+fetch\(\s*['"]data\.json['"]\s*\)\s*\)\.json\(\)''',
                               'await loadSourceData()', script)
assert replacements == 1, 'Expected one source-data fetch in app.js'
loader = '''async function loadSourceData() {
  const response = await fetch('source-data.bin');
  if (!response.ok) throw new Error('Source index HTTP ' + response.status);
  const stream = response.body.pipeThrough(new DecompressionStream('gzip'));
  return JSON.parse(await new Response(stream).text());
}
'''
(destination / 'app.js').write_text(loader + script, encoding='utf8')
assert all(p.stat().st_size < 25 * 1024 * 1024 for p in destination.iterdir())
print(f'Cloudflare package: {len(raw):,} source bytes -> {len(compressed):,} compressed bytes; exact round trip verified.')
