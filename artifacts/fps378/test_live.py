import sys,time,json
from pathlib import Path
sys.path.insert(0,'/app/tests/integration')
import tmux_rendering
from fps_counter import Pane,fps
root=Path('/app/artifacts/fps378-live');root.mkdir(exist_ok=True)
tmux_rendering.SOCKET='ascii378-live'
tmux_rendering.tmux('new-session','-d','-s','keeper')
tmux_rendering.tmux('set-option','-g','remain-on-exit','on')
results=[]
for cols,rows in [(120,40),(80,24)]:
 name=f'{cols}x{rows}'
 argv=['/app/build_ui390/bin/ascii-chat','--no-check-update','--log-level','warn','--log-file',str(root/name/'application.log'),'mirror','--file','/app/artifacts/fps378/media.mp4','--loop','--fps','60','--fps-counter','--audio=false','--splash-screen=false','--color-mode','truecolor']
 pane=Pane(name,argv,root,cols=cols,rows=rows)
 try:
  pane.expect(lambda s:(fps(s) or 0)>0,'startup')
  time.sleep(3)
  samples=[]
  captured=False
  for i in range(30):
   text=pane.capture(); value=fps(text);samples.append(value)
   if value==60 and not captured:
    pane.capture('60fps');captured=True
   time.sleep(.5)
  pane.capture('steady')
  results.append({'size':name,'displayed_fps_samples':samples,'captured_60':captured})
 finally:pane.close()
(root/'results.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))
tmux_rendering.tmux('kill-session','-t','keeper')
