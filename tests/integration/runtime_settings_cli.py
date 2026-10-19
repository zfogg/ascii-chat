"""Exercise real CLI rendering controls in private Windows consoles over TCP and WS.

Console events target only the child process's isolated console. No foreground
window, global keyboard input, or user's terminal is involved.
"""
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from server_lifecycle import Service, free_port, interrupt, server_options


def console_key(pid, key):
    helper = r'''
import ctypes,sys
from ctypes import wintypes as w
k=ctypes.WinDLL('kernel32',use_last_error=True)
k.FreeConsole()
if not k.AttachConsole(int(sys.argv[1])): raise ctypes.WinError(ctypes.get_last_error())
class Char(ctypes.Union): _fields_=[('UnicodeChar',w.WCHAR),('AsciiChar',ctypes.c_char)]
class Key(ctypes.Structure): _fields_=[('down',w.BOOL),('repeat',w.WORD),('vk',w.WORD),('scan',w.WORD),('char',Char),('control',w.DWORD)]
class Event(ctypes.Union): _fields_=[('key',Key),('pad',ctypes.c_byte*16)]
class Record(ctypes.Structure): _fields_=[('type',w.WORD),('event',Event)]
k.CreateFileW.restype=w.HANDLE
k.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.LPVOID,w.DWORD,w.DWORD,w.HANDLE]
k.CloseHandle.argtypes=[w.HANDLE]
h=k.CreateFileW('CONIN$',0xc0000000,3,None,3,0,None)
if h==w.HANDLE(-1).value: raise ctypes.WinError(ctypes.get_last_error())
text=sys.argv[2]
vk=ord(text.upper())
records=(Record*2)()
for i in range(2):
 records[i].type=1; records[i].event.key.down=i==0; records[i].event.key.repeat=1
 records[i].event.key.vk=vk; records[i].event.key.char.UnicodeChar=text
written=w.DWORD()
k.WriteConsoleInputW.argtypes=[w.HANDLE,ctypes.POINTER(Record),w.DWORD,ctypes.POINTER(w.DWORD)]
if not k.WriteConsoleInputW(h,records,2,ctypes.byref(written)): raise ctypes.WinError(ctypes.get_last_error())
k.CloseHandle(h); k.FreeConsole()
'''
    subprocess.run([sys.executable,"-c",helper,str(pid),key],check=True,timeout=10)


def wait_log(path, predicate, process, timeout=25):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        text=path.read_text(encoding="utf-8",errors="replace") if path.exists() else ""
        if predicate(text): return text
        if process.poll() is not None: raise AssertionError(f"CLI exited {process.returncode}: {text[-5000:]}")
        time.sleep(.05)
    raise AssertionError(f"Timed out waiting for settings: {text[-5000:]}")


def check(binary,directory,websocket):
    tcp,ws=free_port(),free_port()
    server=Service(binary,directory,"settings-server-"+str(websocket),"server",server_options(tcp,ws))
    client=None
    try:
        server.wait_for("Server accepting connections")
        log=directory/("settings-client-"+str(websocket)+".log")
        startup=subprocess.STARTUPINFO(); startup.dwFlags=subprocess.STARTF_USESHOWWINDOW; startup.wShowWindow=0
        env={key:value for key,value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
        env.update(CLAUDECODE="1",XDG_CONFIG_HOME=str(directory),APPDATA=str(directory))
        destination=f"ws://127.0.0.1:{ws}" if websocket else f"127.0.0.1:{tcp}"
        client=subprocess.Popen([str(binary),"--no-check-update","--log-file",str(log),"--log-level","debug","client",destination,
          "--test-pattern","--video-codec","raw","--audio=false","--width","40","--height","12","--fps","10",
          "--color-mode","truecolor","--splash-screen=false"],cwd=directory,env=env,startupinfo=startup,
          creationflags=subprocess.CREATE_NEW_CONSOLE)
        wait_log(log,lambda text:"Remote rendering update accepted" in text,client)
        cases=[("c","Color mode:","color=0"),("c","Color mode:","color=1"),("c","Color mode:","color=2"),
               ("c","Color mode:","color=3"),("r","Render mode:","mode=1"),("r","Render mode:","mode=2"),
               ("r","Render mode:","mode=0"),("f","Color filter:","filter=1"),("f","Color filter:","filter=2"),
               ("1","Matrix rain effect:","matrix=1"),("1","Matrix rain effect:","matrix=0")]
        for key,message,effective in cases:
            previous=log.read_text(encoding="utf-8",errors="replace").count(message)
            accepted=log.read_text(encoding="utf-8",errors="replace").count("Remote rendering update accepted")
            previous_server=len(server.text())
            console_key(client.pid,key)
            wait_log(log,lambda text:text.count(message)>previous and text.count("Remote rendering update accepted")>accepted,client)
            wait_log(server.log,lambda text:effective in text[previous_server:],server.process)
        print(f"PASS: {'WebSocket' if websocket else 'TCP'} CLI: {len(cases)} live rendering changes accepted",flush=True)
    finally:
        if client and client.poll() is None:
            interrupt(client)
            try: client.wait(timeout=20)
            finally:
                if client.poll() is None: client.kill(); client.wait()
        server.stop()


def check_snapshots(binary, directory):
    cases = [
        ("none", "minimal", "foreground", None),
        ("16", "blocks", "foreground", None),
        ("256", "digital", "foreground", None),
        ("truecolor", "cool", "background", None),
        ("truecolor", "custom", "half-block", " .+#"),
        ("truecolor", "standard", "foreground", None),
    ]
    env = {key: value for key, value in os.environ.items() if not key.startswith("ASCII_CHAT_")}
    env.update(CLAUDECODE="1", XDG_CONFIG_HOME=str(directory), APPDATA=str(directory))
    for index, (color, palette, rendering, custom) in enumerate(cases):
        command = [str(binary), "--no-check-update", "--log-level", "error", "--log-file", str(directory/f"mirror-{index}.log"),
                   "mirror", "--test-pattern", "--audio=false", "--snapshot", "--snapshot-delay", "0",
                   "--width", "32", "--height", "12", "--color-mode", color, "--palette", palette,
                   "--render-mode", rendering, "--utf8", "true", "--splash-screen=false"]
        if custom: command += ["--palette-chars", custom]
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=30)
        assert result.returncode == 0, result.stderr.decode("utf-8", errors="replace")
        frame = result.stdout.decode("utf-8")
        expanded = re.sub(r"(.)\x1b\[(\d+)b", lambda match: match[1]*(int(match[2])+1), frame)
        plain = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", expanded)
        assert len(plain) >= 32*12 and len(plain.splitlines()) >= 12, repr(frame[-500:])
        if color == "none": assert "\x1b[38;" not in frame and "\x1b[48;" not in frame
        if color == "256": assert "\x1b[38;5;" in frame
        if color == "truecolor" and rendering == "foreground": assert "\x1b[38;2;" in frame
        if rendering == "background": assert "\x1b[48;2;" in frame
        if rendering == "half-block": assert "▀" in frame or "█" in frame
    print(f"PASS: {len(cases)} native snapshots: four color modes, six palettes, three rendering modes", flush=True)


if __name__=="__main__":
    if os.name!="nt": raise SystemExit("This fixture uses isolated Windows consoles")
    binary=Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ascii-runtime-settings-") as temporary:
        directory=Path(temporary)
        if len(sys.argv) < 3 or sys.argv[2] != "snapshots":
            check(binary,directory,False)
            check(binary,directory,True)
        check_snapshots(binary,directory)
