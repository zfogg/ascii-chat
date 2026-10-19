const fs = require('fs');
const path = require('path');
const http = require('http');
const {chromium} = require(process.env.PLAYWRIGHT_MODULE || path.join(__dirname,'terminal/node_modules/@playwright/test'));
const root = path.resolve(process.argv[2]);
const dest = path.resolve(process.argv[3]);
fs.mkdirSync(dest, {recursive:true});
const groups=[];
for (const host of fs.readdirSync(root)) {
  if (!host.endsWith('-frames')) continue;
  const file=path.join(root,host,'manifest.json');
  if (!fs.existsSync(file)) continue;
  const manifest=JSON.parse(fs.readFileSync(file));
  for (const backend of [...new Set(manifest.cases.map(c=>c.backend))])
    for (const pattern of [0,1]) {
      const cases=manifest.cases.filter(c=>c.backend===backend && c.pattern===pattern);
      if(cases.length!==12) throw Error(`Incomplete ${host} ${backend} ${pattern}: ${cases.length}`);
      groups.push({id:`${host}-${backend}-pattern${pattern}`,host,backend,pattern,manifest,cases});
    }
}
const server=http.createServer((req,res)=>{
  const url=new URL(req.url,'http://localhost');
  if(url.pathname==='/xterm.js') {res.setHeader('Content-Type','text/javascript');res.end(fs.readFileSync(path.join(__dirname,'terminal/node_modules/@xterm/xterm/lib/xterm.js')));return;}
  if(url.pathname==='/xterm.css') {res.setHeader('Content-Type','text/css');res.end(fs.readFileSync(path.join(__dirname,'terminal/node_modules/@xterm/xterm/css/xterm.css')));return;}
  const group=groups[Number(url.searchParams.get('group'))];
  if(!group) {res.writeHead(404);res.end();return;}
  const data=group.cases.map(c=>({...c,ansi:fs.readFileSync(path.join(root,group.host,c.name+'.ansi')).toString('base64')}));
  res.setHeader('Content-Type','text/html; charset=utf-8');
  res.end(`<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/xterm.css">
<style>body{margin:0;padding:18px;background:#18212c;color:#e5eaf0;font:16px sans-serif}h1{font-size:23px;margin:0 0 7px}p{margin:5px 0 14px}main{display:grid;grid-template-columns:repeat(4,max-content);gap:14px}.case{background:#080a0d;padding:10px;border:1px solid #506070}h2{font-size:16px;margin:0 0 9px}.xterm{padding:0}.xterm-viewport{overflow:hidden!important}</style>
<h1>${group.host} · ${group.backend.toUpperCase()} · test pattern ${group.pattern} · 120×40</h1>
<p>Unmodified PTY capture replay · source ${group.manifest.source_sha} · columns: mono / 16 / 256 / truecolor · rows: foreground / background / half-block</p>
<main></main><script src="/xterm.js"></script><script>
const cases=${JSON.stringify(data)};window.results=[];window.terms=[];
(async()=>{for(const c of cases){const el=document.createElement('section');el.className='case';el.innerHTML='<h2>'+c.render+' / '+c.color+' · exit '+c.exit+'</h2><div></div>';document.querySelector('main').append(el);
const t=new Terminal({cols:124,rows:44,scrollback:200,allowProposedApi:true,fontFamily:'Consolas, monospace',fontSize:12,lineHeight:1,letterSpacing:0,theme:{background:'#000000',foreground:'#ffffff'},cursorBlink:false});t.open(el.lastChild);window.terms.push(t);
const raw=Uint8Array.from(atob(c.ansi),ch=>ch.charCodeAt(0));await new Promise(r=>t.write(raw,r));
const b=t.buffer.active;let lines=[],colors=new Set(),ink=0,replacement=0,bgPainted=0,fgPainted=0;
for(let y=0;y<b.length;y++){const line=b.getLine(y);lines.push(line.translateToString(true));for(let x=0;x<124;x++){const cell=line.getCell(x);if(!cell.isBgDefault())bgPainted++;if(!cell.isFgDefault())fgPainted++;if(cell.getChars().trim()){ink++;if(cell.getChars().includes('�'))replacement++;colors.add(cell.getFgColorMode()+':'+cell.getFgColor()+':'+cell.getBgColorMode()+':'+cell.getBgColor());}}}
window.results.push({name:c.name,exit:c.exit,ink,replacement,bgPainted,fgPainted,colorPairs:colors.size,lines});}
await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));window.ready=true;})();</script>`);
});
(async()=>{
  await new Promise(r=>server.listen(0,'127.0.0.1',r));
  const browser=await chromium.launch({headless:true});
  const page=await browser.newPage({viewport:{width:4000,height:1900},deviceScaleFactor:1});
  const summary=[];
  for(let i=0;i<groups.length;i++){
    await page.goto(`http://127.0.0.1:${server.address().port}/?group=${i}`);
    await page.waitForFunction(()=>window.ready===true);
    await page.screenshot({path:path.join(dest,groups[i].id+'.png'),fullPage:true});
    const results=await page.evaluate(()=>window.results);
    fs.writeFileSync(path.join(dest,groups[i].id+'.cells.json'),JSON.stringify(results,null,2));
    summary.push({id:groups[i].id,cases:results.map(({lines,...rest})=>rest)});
    console.log(groups[i].id,results.map(r=>`${r.ink}/${r.colorPairs}/${r.replacement}`).join(' '));
  }
  fs.writeFileSync(path.join(dest,'screenshots.json'),JSON.stringify(summary,null,2));
  await browser.close();server.close();
})().catch(e=>{console.error(e);process.exit(1)});
