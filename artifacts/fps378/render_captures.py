from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import re
root=Path('artifacts/fps378')
font=ImageFont.truetype('C:/Windows/Fonts/consola.ttf',16)
colors=['000000','800000','008000','808000','000080','800080','008080','c0c0c0','808080','ff0000','00ff00','ffff00','0000ff','ff00ff','00ffff','ffffff']
def color(n):
    if n<16:return '#'+colors[n]
    if n>=232:return (8+10*(n-232),)*3
    n-=16; vals=[0,95,135,175,215,255];return (vals[n//36],vals[n//6%6],vals[n%6])
for name in ['controls-foreground/enabled','controls-foreground/help-forced','controls-background/enabled','controls-half-block/enabled','benchmark-120x40/steady','benchmark-160x50/steady']:
    path=root/(name+'.ansi')
    if not path.exists():continue
    rows=path.read_text(encoding='utf-8').splitlines();width=max(len(re.sub(r'\x1b\[[0-9;]*m','',line)) for line in rows)
    im=Image.new('RGB',(width*10,len(rows)*20),'#101010');d=ImageDraw.Draw(im)
    fg,bg,reverse='#dddddd','#101010',False
    for y,line in enumerate(rows):
        x=0
        for token in re.split(r'(\x1b\[[0-9;]*m)',line):
            if token.startswith('\x1b['):
                codes=[int(v or 0) for v in token[2:-1].split(';')];i=0
                while i<len(codes):
                    c=codes[i]
                    if c==0:fg,bg,reverse='#dddddd','#101010',False
                    elif c==7:reverse=True
                    elif c==27:reverse=False
                    elif c==39:fg='#dddddd'
                    elif c==49:bg='#101010'
                    elif 30<=c<=37:fg=color(c-30)
                    elif 40<=c<=47:bg=color(c-40)
                    elif 90<=c<=97:fg=color(c-90+8)
                    elif 100<=c<=107:bg=color(c-100+8)
                    elif c in (38,48) and i+2<len(codes):
                        if codes[i+1]==2:v=tuple(codes[i+2:i+5]);i+=4
                        else:v=color(codes[i+2]);i+=2
                        if c==38:fg=v
                        else:bg=v
                    i+=1
            else:
                for ch in token:
                    a,b=(bg,fg) if reverse else (fg,bg)
                    d.rectangle((x*10,y*20,x*10+9,y*20+19),fill=b);d.text((x*10,y*20),ch,font=font,fill=a);x+=1
    im.save(root/(name+'.png'))
