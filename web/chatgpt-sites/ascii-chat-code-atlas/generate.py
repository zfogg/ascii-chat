from pathlib import Path
import re,json,subprocess,collections,bisect
from guides import make_guides
C_EXTENSIONS={'.c','.cpp','.m'}
root=Path(__file__).resolve().parents[3]
out=Path(__file__).parent/'dist'
paths=[]
for folder in ['src','lib','web/web/src','web/packages/shared/src']:
 for p in (root/folder).rglob('*'):
  if p.suffix in ['.c','.cpp','.m','.ts','.tsx','.js'] and not any(x in p.parts for x in ['dist','node_modules','test','tests']) and not re.search(r'\.(test|spec)\.',p.name): paths.append(p)
def reference_text(text, clean, match):
 start=match.start()
 if match.group(2): return text[start:match.end()].strip()
 while start>0 and (text[start-1].isalnum() or text[start-1] in '_.>-' ): start-=1
 depth=1; end=match.end()
 while depth and end<len(clean):
  depth+=(clean[end]=='(')-(clean[end]==')');end+=1
 return text[start:end]

nodes=[]; symbols=collections.defaultdict(list); contents={}; file_symbols={}; definition_ranges={}
# Lexical source index: intentionally not a compiler call graph.
cdef=re.compile(r'^\s*(?:(?:static|inline|extern|EMSCRIPTEN_KEEPALIVE)\s+)*(?:[\w]+[\s*]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{',re.M)
tsdef=re.compile(r'(?:export\s+)?(?:async\s+)?function\s+(\w+)(?:<[^;{}]*>)?\s*\(|(?:export\s+)?class\s+(\w+)\b|(?:export\s+)?const\s+(\w+)\s*=\s*(?:async\s*)?(?:\([^;]*?\)|\w+)\s*=>')
for p in paths:
 id=p.relative_to(root).as_posix(); text=p.read_text(encoding='utf-8',errors='replace'); contents[id]=text
 funcs=[]; definition_ranges[id]=[]
 masked=re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""",lambda m: "".join("\n" if c=="\n" else " " for c in m.group()),text,flags=re.S)
 for m in (cdef if p.suffix in C_EXTENSIONS else tsdef).finditer(masked):
  definition_ranges[id].append((m.start(),m.end()))
  name=next(x for x in m.groups() if x); line=text[:m.start()].count('\n')+1
  if name in ['if','while','switch','for']:continue
  endline=line
  start=m.start()
  while start<len(text) and masked[start].isspace(): start+=1
  line=text[:start].count('\n')+1
  if p.suffix in C_EXTENSIONS:
   depth=1; pos=m.end()
   while depth and pos<len(masked):
    depth+=(masked[pos]=='{')-(masked[pos]=='}');pos+=1
   endline=text[:pos].count('\n')+1
  funcs.append({'name':name,'line':line,'end':endline,'snippet':text[start:pos] if p.suffix in C_EXTENSIONS else text[start:m.end()]})
  if (p.suffix in C_EXTENSIONS and not re.search(r'\bstatic\b',m.group())) or (p.suffix not in C_EXTENSIONS and m.group().lstrip().startswith('export')): symbols[name].append(id)
 file_symbols[id]={f['name'] for f in funcs}
 kind='lib' if id.startswith('lib/') else 'wasm' if id.startswith('src/web/') else 'ts' if id.startswith('web/') else 'c'
 nodes.append({'id':id,'name':p.name,'kind':kind,'functions':funcs,'lines':len(text.splitlines()),'preview':'\n'.join(text.splitlines()[:24])})
ids={n['id'] for n in nodes}; edges={}; local_edges={}
for id,text in contents.items():
 clean=re.sub(r"""/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`""",lambda m: ''.join('\n' if c=='\n' else ' ' for c in m.group()),text,flags=re.S)
 starts=[x[0] for x in definition_ranges[id]]
 for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(|(?:\.|->)\w+\s*=\s*(?:&\s*)?(\w+)',clean):
  header=bisect.bisect_right(starts,m.start())-1
  if header>=0 and m.start()<definition_ranges[id][header][1]:continue
  symbol=m.group(1) or m.group(2); candidates=[id] if symbol in file_symbols[id] else symbols.get(symbol,[])
  if not candidates and symbol.startswith('_'):candidates=symbols.get(symbol[1:],[])
  candidates=[target for target in candidates if (Path(target).suffix in C_EXTENSIONS if Path(id).suffix in C_EXTENSIONS or symbol.startswith('_') else Path(target).suffix not in C_EXTENSIONS)]
  if len(candidates)!=1:continue
  target=candidates[0]; key=(id,target); line=clean[:m.start()].count('\n')+1
  item=(local_edges if target==id else edges).setdefault(key,{'source':id,'target':target,'kind':'reference','evidence':[]})
  if not any(x['symbol']==symbol and x['line']==line for x in item['evidence']):item['evidence'].append({'symbol':symbol,'line':line,'text':reference_text(text,clean,m)})
 for m in re.finditer(r'from\s+[\"\']([^\"\']+)[\"\']',text):
  imp=m.group(1)
  if not imp.startswith('.'):continue
  base=(root/id).parent/imp
  for suffix in ['.ts','.tsx','/index.ts','/index.tsx']:
   target=Path(str(base)+suffix).resolve().relative_to(root).as_posix()
   if target in ids:
    edges.setdefault((id,target),{'source':id,'target':target,'kind':'import','evidence':[{'symbol':imp,'line':text[:m.start()].count('\n')+1,'text':m.group()}]});break

 for resource in re.finditer(r'new\s+URL\(\s*["\']([^"\']+)["\']\s*,\s*import\.meta\.url\s*\)',text):
  ref=resource.group(1)
  if not ref.startswith('.'):continue
  target=((root/id).parent/ref).resolve().relative_to(root).as_posix()
  if target in ids:
   edges.setdefault((id,target),{'source':id,'target':target,'kind':'resource','evidence':[{'symbol':ref,'line':text[:resource.start()].count('\n')+1,'text':resource.group()}]})


views=make_guides()
assert len(views)==18 and len({v['id'] for v in views})==18
node_index={n['id']:n for n in nodes}
for v in views:
 def columns(steps):
  seen=set(); result=[]; titles=[]
  for step in steps:
   column=[]
   for file,symbol in [(step['file'],step['symbol'])]+[(f,None) for f in step['also']]:
    if file not in node_index: raise ValueError(f"Missing guide file {v['id']}: {file}")
    if symbol and not any(f['name']==symbol for f in node_index[file]['functions']): raise ValueError(f"Missing guide symbol {v['id']}: {file}:{symbol}")
    key=file+'::'+symbol if symbol else file
    if key not in seen: column.append(key);seen.add(key)
   if column: result.append(column);titles.append(step['title'])
  return result,titles
 for chapter in v['chapters']:
  chapter['columns'],chapter['columnTitles']=columns(chapter['steps'])
 seen=set();v['columns']=[];v['columnTitles']=[]
 for chapter in (v['chapters'] if len(v['chapters'])<=4 else v['chapters'][::3]):
  col=[]
  for step in chapter['steps']:
   key=step['file']+'::'+step['symbol'] if step['symbol'] else step['file']
   if key not in seen: col.append(key);seen.add(key)
  if col:v['columns'].append(col);v['columnTitles'].append(chapter['title'])
 for related in v['related']:assert any(x['id']==related for x in views)

summary={
'web/packages/shared/src/components/AsciiRenderer/useAsciiRendererHandle.ts':'Connects the React renderer handle and browser input to a C UI session via the TypeScript SessionUi wrapper.',
'src/main.c':'Executable entry point: process initialization, options, and mode dispatch.',
'src/common/session/client_like.c':'Shared lifecycle for native mirror, client, and discovery: initialize display/capture/audio, run session attempts, then clean up.',
'src/common/session/capture.c':'Shared native capture context and media-source integration.',
'src/common/session/display.c':'Native display context, terminal output, UI coordination, and rendering.',
'src/common/session/host.c':'Reusable host session: client membership, incoming packets, and video rendering.',
'src/web/mirror.c':'Browser-callable C entry points, including mirror_convert_frame: converts RGBA frames into ASCII output.',
'src/web/client.c':'WASM client exports for initialization, cryptographic handshakes, and packet processing; transport callbacks cross into JavaScript.',
'src/web/browser_host.c':'Adapts browser transport callbacks to the shared C session host.',
'src/web/common/ui.c':'Thin Emscripten exports forwarding resize, keys, media, and tick events to lib/ui/session.c.',
'lib/ui/session.c':'Nonblocking C UI state and rendering driven by host ticks and input.',
'web/packages/shared/src/wasm/sessionUi.ts':'Owns one C UI instance; marshals strings and input into WASM and reads generated output.',
'web/web/src/pages/Mirror.tsx':'Composes browser media sources, WASM initialization, settings, the frame loop, and AsciiRenderer.',
'web/packages/shared/src/hooks/useMirrorRenderLoop.ts':'Schedules browser frame conversion and forwards output to the renderer.',
'web/web/src/network/ClientConnection.ts':'Browser connection state and transport integration with the C/WASM client.',
'web/packages/shared/src/wasm/mirror.ts':'TypeScript interface and memory marshalling for the mirror WASM module.'}
for n in nodes:n['description']=summary.get(n['id'], ('Reusable C implementation for '+n['id'].split('/')[1]+'.') if n['kind']=='lib' else 'Source module in '+str(Path(n['id']).parent).replace('\\','/')+'. Select a symbol or connection to inspect the source evidence.')
sha=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
data={'sha':sha,'nodes':nodes,'edges':list(edges.values()),'localEdges':list(local_edges.values()),'views':views,'note':'Source snapshot of working tree on 2026-10-18. Lexical references and relative imports, not a compiler-verified call graph. Conditional compilation, callbacks, macros, barrel exports, and dynamic dispatch may be incomplete. Guided maps show a selected subset; use Explore file for adjacent dependencies. Generated WASM artifacts are excluded.'}
(out/'data.json').write_text(json.dumps(data,separators=(',',':')),encoding='utf8')
print(f'{len(views)} guides, {sum(len(v["chapters"]) for v in views)} chapters; {len(nodes)} files, {sum(len(n["functions"]) for n in nodes)} symbols, {len(edges)} source relationships; {len(json.dumps(data))//1024} KiB')
