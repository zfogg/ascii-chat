#!/usr/bin/env python3
"""Measure native mode CPU on Windows/Linux; 100% equals one busy logical core.

Uses loopback servers and synthetic media. Results include exit codes: an exited
process with zero CPU is not an idle success. Every owned process is reaped.
"""

import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--filter', default='', help='Run scenario names containing this text')
parser.add_argument('--seconds', type=int, default=5)
parser.add_argument('--warmup', type=float, default=2)
options = parser.parse_args()
if options.seconds < 1 or options.warmup < 0:
    parser.error('seconds must be positive and warmup must be nonnegative')
if os.name != 'nt' and not sys.platform.startswith('linux'):
    parser.error('CPU counters currently support Windows and Linux')
ROOT = Path(__file__).resolve().parents[2]
OUT = options.output.resolve()
OUT.mkdir(parents=True, exist_ok=True)
EXE = options.binary.resolve()
WINDOWS = os.name == 'nt'
if WINDOWS:
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4

def cpu(p):
    if not WINDOWS:
        try:
            fields = Path('/proc/%d/stat' % p.pid).read_text().split(') ',1)[1].split()
            p.last_cpu = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
        except FileNotFoundError:
            pass
        return getattr(p, 'last_cpu', 0)
    ft = [wintypes.FILETIME() for _ in range(4)]
    if not k32.GetProcessTimes(int(p._handle), *[ctypes.byref(f) for f in ft]):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum((f.dwHighDateTime << 32) + f.dwLowDateTime for f in ft[2:]) / 1e7

def launch(name, args, output='null', stdin='null'):
    err = open(OUT / (name + '.stderr'), 'wb')
    dest = subprocess.DEVNULL if output == 'null' else subprocess.PIPE
    inp = subprocess.DEVNULL if stdin == 'null' else subprocess.PIPE
    argv = [str(EXE), '--no-check-update', '--log-level', 'warn', '--log-file', str(OUT / (name + '.log'))] + args
    # The matrix connects only to its own loopback peers, without modifying known_hosts.
    env = dict(os.environ, ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK='1', LSAN_OPTIONS='detect_leaks=0')
    p = subprocess.Popen(argv, cwd=ROOT, stdin=inp, stdout=dest, stderr=err, env=env,
                         creationflags=subprocess.CREATE_NO_WINDOW if WINDOWS else 0)
    err.close()
    p.argv = argv
    return p

def run(name, args, server=None, action=None, output='null', stdin='null', seconds=None):
    seconds = options.seconds if seconds is None else seconds
    processes = []
    try:
        if server is not None:
            s = launch(name + '-server', ['server', '--port', '38451', '--websocket-port', '38452', '--status-screen=false'] + server)
            processes.append(s)
            time.sleep(1)
        p = launch(name, args, output, stdin)
        processes.append(p)
        time.sleep(options.warmup)
        if action == 'kill-server':
            s.kill(); s.wait()
        elif action == 'close-output' and p.stdout:
            p.stdout.close()
        elif action == 'close-input' and p.stdin:
            p.stdin.close()
        t = time.monotonic()
        before = [cpu(x) for x in processes]
        samples = []
        last = before
        for _ in range(seconds):
            time.sleep(1)
            now = [cpu(x) for x in processes]
            samples.append([round(100*(a-b), 2) for a,b in zip(now,last)])
            last = now
        elapsed = time.monotonic() - t
        result = {'name': name, 'commands': [x.argv for x in processes],
                  'cpu_pct': [round(100*(a-b)/elapsed,2) for a,b in zip(last,before)],
                  'samples': samples, 'exit_codes': [x.poll() for x in processes],
                  'pids': [x.pid for x in processes], 'seconds': elapsed,
                  'action': action, 'output': output, 'stdin': stdin}
        with open(OUT / 'results.jsonl', 'a') as f:
            f.write(json.dumps(result) + '\n')
        print(json.dumps({k:result[k] for k in ['name','cpu_pct','exit_codes']}), flush=True)
    finally:
        for p in reversed(processes):
            if p.poll() is None:
                p.kill()
            p.wait()
            if p.stdin and not p.stdin.closed: p.stdin.close()
            if p.stdout and not p.stdout.closed: p.stdout.close()

client = ['client','127.0.0.1:38451','--test-pattern','--audio=false','--splash-screen=false','--width','80','--height','24']
mirror = ['mirror','--test-pattern','--audio=false','--splash-screen=false','--width','80','--height','24']
filebase = ['mirror','--file','tests/fixtures/test_pattern.mp4','--audio=false','--splash-screen=false','--width','80','--height','24']
cases = []
for flags in [[],['--no-audio-mixer'],['--status-screen'],['--no-encrypt'],['--no-compress']]:
    cases.append(('server-' + str(len(cases)), ['server','--port','38451','--websocket-port','38452','--status-screen=false']+flags, {}))
for flags in [[],['--reconnect-attempts','0'],['--splash-screen'],['--snapshot','--snapshot-delay','0']]:
    cases.append(('unreachable-'+str(len(cases)), client+flags, {}))
for label,flags,sflags in [('default',[],[]),('raw',['--video-codec','raw'],[]),('no-encrypt',['--no-encrypt'],['--no-encrypt']),('no-compress',['--no-compress'],['--no-compress']),('audio',['--audio'],[]),('fps1',['--fps','1'],[]),('fps144',['--fps','144'],[]),('snapshot',['--snapshot','--snapshot-delay','0'],[]),('snapshot3',['--snapshot','--snapshot-delay','3'],[]),('waveform',['--audio','--audio-capture-source','remote','--no-audio-playback','--waveform'],[]),('fft',['--audio','--audio-capture-source','remote','--no-audio-playback','--fft'],[]),('password',['--password','test-password'],['--password','test-password']),('wrong-password',['--password','wrong-password'],['--password','test-password'])]:
    cases.append(('client-'+label,client+flags,{'server':sflags}))
cases += [('server-loss',client,{'server':[], 'action':'kill-server'}),('blocked-output',client,{'server':[], 'output':'pipe'}),('closed-output',client,{'server':[], 'output':'pipe','action':'close-output'}),('closed-input',client,{'server':[], 'stdin':'pipe','action':'close-input'})]
for label,flags in [('default',[]),('fps1',['--fps','1']),('fps144',['--fps','144']),('audio',['--audio']),('waveform',['--audio','--audio-capture-source','remote','--waveform']),('fft',['--audio','--audio-capture-source','remote','--fft']),('snapshot',['--snapshot','--snapshot-delay','0']),('matrix',['--matrix']),('color',['--color-mode','truecolor','--render-mode','half-block']),('large',['--width','240','--height','80'])]:
    cases.append(('mirror-'+label,mirror+flags,{}))
for label,flags in [('eof',[]),('loop',['--loop']),('pause',['--pause']),('seek',['--seek','999']),('missing',['--file','missing-cpu-fixture.mp4'])]:
    cases.append(('file-'+label,filebase+flags,{}))
cases += [('acds', ['discovery-service','--port','38453','--websocket-port','38454','--database',str(OUT/'acds.db'),'--status-screen=false'],{}),('discovery-unreachable',['--discovery-service','127.0.0.1','--discovery-service-port','38453','--test-pattern','--audio=false','--splash-screen=false'],{})]
if __name__ == '__main__':
    for name,args,kwargs in cases:
        if options.filter in name:
            run(name,args,**kwargs)

