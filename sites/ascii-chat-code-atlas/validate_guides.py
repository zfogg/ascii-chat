"""Check the authored journeys against the generated, parsed source snapshot."""
from pathlib import Path
import json

base=Path(__file__).resolve().parent
data=json.loads((base/'dist/data.json').read_text(encoding='utf8'))
nodes={node['id']:node for node in data['nodes']}
guides={guide['id']:guide for guide in data['views']}
original={'native','mirror','client','ui','discovery'}
assert original<=guides.keys()
assert len(guides)==17 and len(guides.keys()-original)==12
references=0
files=set()
for guide in guides.values():
 assert all(guide[field] for field in ['question','goal','outcome','diagnostic','scope','group'])
 assert len(guide['chapters'])>=3
 assert len({chapter['id'] for chapter in guide['chapters']})==len(guide['chapters'])
 assert all(related in guides for related in guide['related'])
 for chapter in guide['chapters']:
  assert chapter['summary'] and len(chapter['steps'])>=3
  columns=[key for column in chapter['columns'] for key in column]
  assert len(columns)==len(set(columns))
  for step in chapter['steps']:
   assert step['file'] in nodes and step['title'] and step['text']
   if step['symbol']:
    fn=next((f for f in nodes[step['file']]['functions'] if f['name']==step['symbol']),None)
    assert fn is not None,(guide['id'],step['file'],step['symbol'])
    source=(base.parents[1]/step['file']).read_text(encoding='utf8',errors='replace')
    assert fn['snippet'] in source,(guide['id'],step['file'],step['symbol'])
   for file in step['also']:assert file in nodes
   files.add(step['file']);references+=1
print(f'PASS: 5 enhanced + 12 new guides; {sum(len(g["chapters"]) for g in guides.values())} chapters; {references} exact source checkpoints across {len(files)} primary source files.')
