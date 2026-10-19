from pathlib import Path
import subprocess
base=Path(__file__).resolve().parent
subprocess.run(['node', str(base/'complete-references.mjs')], cwd=base, check=True)
subprocess.run(['python', str(base/'validate_guides.py')], cwd=base, check=True)
subprocess.run(['node', str(base/'highlight.mjs')], cwd=base, check=True)
html=(base/'dist/index.html').read_text(encoding='utf8')
css=(base/'dist/style.css').read_text(encoding='utf8')
js=(base/'dist/app.js').read_text(encoding='utf8')
guide_js=(base/'dist/guides.js').read_text(encoding='utf8')
data=(base/'dist/data.json').read_text(encoding='utf8').replace('<','\\u003c')
html=html.replace('<script src="guides.js"></script>','<script>'+guide_js+'</script>')
html=html.replace('<link rel="stylesheet" href="style.css">','<style>'+css+'</style>').replace('<script src="app.js"></script>','<script id="source-data" type="application/json">'+data+'</script><script>'+js+'</script>')
(base/'ascii-chat-code-atlas.html').write_text(html,encoding='utf8')
print('Packaged standalone HTML')
