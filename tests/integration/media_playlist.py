"""Smoke-test ordered CLI artifacts, sequential drops, centered UI and skip/restart."""

# Apply the shared test policy before importing helpers or starting native children.
from pathlib import Path as _TestPath
import sys as _test_sys
_test_sys.path.insert(0, str(next(p for p in _TestPath(__file__).resolve().parents if (p / "tests" / "_environment.py").is_file()) / "tests"))
import _environment  # noqa: F401

import argparse
import functools
import http.server
import os
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from terminal_ui import Terminal
if os.name == "nt":
    from windows_console import WindowsConsole as Terminal


def centered(term, title):
    text = term.pump(0.03)
    lines = term.screen.display
    title_row = next(i for i,line in enumerate(lines) if title in line)
    title_col = lines[title_row].index(title)
    assert abs(title_col + len(title)/2 - term.screen.columns/2) <= 1, ("Title is not centered", lines)
    assert sum("___" in line for line in lines[:title_row]) >= 2 or any("ascii-chat" in line for line in lines[:title_row]), "Shared splash logo missing"
    logo_colors = {cell.fg for y,row in term.screen.buffer.items() if y < title_row for cell in row.values() if cell.data.strip()}
    assert len(logo_colors) >= 3, ("Splash header must retain rainbow colors", logo_colors)
    ink = [(x,y) for y,line in enumerate(lines) for x,char in enumerate(line) if char != ' ']
    assert abs((min(x for x,y in ink)+max(x for x,y in ink))/2 - (term.screen.columns-1)/2) <= 1, ("Panel is not horizontally centered",text)
    occupied = [i for i,line in enumerate(lines) if line.strip()]
    assert abs((occupied[0]+occupied[-1])/2 - (term.screen.lines-1)/2) <= 1, ("Panel is not vertically centered", text)


def save_screen(term, path):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return
    fonts = [Path("C:/Windows/Fonts/consola.ttf"), Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf")]
    font = next((ImageFont.truetype(str(p),18) for p in fonts if p.exists()), ImageFont.load_default())
    width, height = 12, 24
    image = Image.new("RGB",(term.screen.columns*width,term.screen.lines*height),(12,12,18))
    draw = ImageDraw.Draw(image)
    colors = {"default":"#dddddd", "black":"#222222", "red":"#cd5555", "green":"#55cd55", "yellow":"#cdcd55", "brown":"#cdcd55",
              "blue":"#5555cd", "magenta":"#cd55cd", "cyan":"#55cdcd", "white":"#dddddd"}
    for y,row in term.screen.buffer.items():
        for x,cell in row.items():
            name = cell.fg.removeprefix("bright")
            color = colors.get(name,"#"+name if len(name)==6 else "#dddddd")
            draw.text((x*width,y*height),cell.data,font=font,fill=color)
    image.save(path)


def colored(term, channel):
    def check(text):
        if "Open media:" in text or "Loading media" in text: return False
        found = 0
        for row in term.screen.buffer.values():
            for cell in row.values():
                value = cell.fg
                if value in ({"red","brightred"} if channel==0 else {"blue","brightblue"}): found+=1
                if len(value)==6 and all(c in "0123456789abcdef" for c in value.lower()):
                    rgb=[int(value[i:i+2],16) for i in (0,2,4)]
                    if rgb[channel]>80 and rgb[channel]>max(rgb[(channel+1)%3],rgb[(channel+2)%3])*2: found+=1
        return found>100
    return check


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--binary",type=Path,required=True)
    parser.add_argument("--artifacts",type=Path,required=True)
    args=parser.parse_args()
    args.artifacts.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ascii-playlist-") as folder:
        root=Path(folder)/"a long media directory that exercises horizontal path scrolling"
        root.mkdir()
        files=[root/"first red.mp4",root/"second blue.mp4"]
        for index,path in enumerate(files):
            r="random(1)*255" if index==0 else "0"
            b="random(1)*255" if index==1 else "0"
            subprocess.run(["ffmpeg","-v","error","-y","-f","lavfi","-i",
                            f"nullsrc=s=120x90:r=20,geq=r='{r}':g=0:b='{b}'",
                            "-t","3","-c:v","libx264","-pix_fmt","yuv420p","-movflags","+faststart",str(path)],
                           check=True,timeout=30)
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                time.sleep(0.6)
                try:
                    super().do_GET()
                except ConnectionError:
                    pass
        server=http.server.ThreadingHTTPServer(("127.0.0.1",0),functools.partial(Handler,directory=str(root)))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        url=f"http://127.0.0.1:{server.server_port}/second%20blue.mp4"
        argv=[str(args.binary.resolve()),"--no-check-update","--log-level","info","--log-file",
              str((args.artifacts/"interactive.log").resolve()),"mirror","--audio=false","--fps","20",
              "--color-mode","truecolor","--splash-screen=false"]
        term=Terminal(argv+["--test-pattern"],rows=40,cols=140)
        try:
            term.expect(lambda t:t.count("oo")>15,"Initial test pattern",timeout=90)
            term.write("?")
            term.expect(lambda t:"< / >" in t and "Previous/next" in t, "Playlist skip shortcuts appear in keyboard help")
            save_screen(term,args.artifacts/"keyboard-help.png")
            term.write("\x1b")
            term.expect(lambda t:t.count("oo")>15,"Close keyboard help")
            term.write("0")
            term.expect(lambda t:"Sync primitives" in t, "Debug sampling starts when its overlay opens")
            term.write("0")
            term.expect(lambda t:t.count("oo")>15, "Close sync overlay")
            term.write("o")
            term.expect(lambda t:"Open media:" in t,"Open editor")
            centered(term,"Open media:")
            for path in files:
                # Explorer emits a quoted filename followed by a space.
                term.write('"'+str(path)+'" ')
                term.expect(lambda t:path.name in t,"Dropped path appears")
                def cursor_after_quote(_):
                    x,y = term.cursor if os.name == "nt" else (term.screen.cursor.x,term.screen.cursor.y)
                    line = term.screen.display[y]
                    return x > 0 and line[x-1] == '"' and '> "' in line and path.name in line
                term.expect(cursor_after_quote, "Drop cursor follows the closing quote without a trailing space")
            centered(term,"Open media:")
            save_screen(term,args.artifacts/"editor.png")
            term.write("\r")
            term.expect(colored(term,0),"First dropped artifact plays",timeout=30)
            term.expect(colored(term,2),"EOF advances to second artifact",timeout=15)
            term.write("<")
            term.expect(colored(term,0),"Previous artifact restarts",timeout=30)
            term.write(">")
            term.expect(colored(term,2),"Next artifact restarts",timeout=30)
            term.write("o")
            term.expect(lambda t:"Open media:" in t,"Reopen editor")
            term.write(url)
            term.expect(lambda t:"blue.mp4" in t,"URL appears")
            term.write("\r")
            term.expect(lambda t:"Loading media..." in t,"Loading UI remains visible")
            centered(term,"Loading media...")
            save_screen(term,args.artifacts/"loading.png")
            term.expect(colored(term,2),"URL replacement completes",timeout=30)
            (args.artifacts/"terminal.txt").write_text("\n".join(term.screen.display),encoding="utf-8")
            term.interrupt()
        finally:
            term.close()
        # Repeat the same ordered playlist through CLI --file / --url arguments.
        argv[argv.index(str((args.artifacts/"interactive.log").resolve()))]=str((args.artifacts/"cli.log").resolve())
        term=Terminal(argv+["--file",str(files[0]),"--url",url,"--loop"],rows=30,cols=100)
        try:
            term.expect(colored(term,0),"First CLI artifact",timeout=90)
            term.expect(colored(term,2),"Mixed CLI URL follows file",timeout=20)
            term.expect(colored(term,0),"CLI --loop repeats the playlist",timeout=20)
            term.write(">")
            term.expect(colored(term,2),"CLI next artifact restarts",timeout=30)
            term.write("<")
            term.expect(colored(term,0),"CLI previous artifact restarts",timeout=30)
            term.interrupt()
        finally:
            term.close()
            server.shutdown()
        print("PASS centered rainbow panels, trimmed repeated drops, EOF playlist, CLI order and < / > restart")

if __name__=="__main__":
    main()
