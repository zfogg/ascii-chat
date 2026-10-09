"""Opt-in Windows live test: creates two UDP mappings and removes them on exit.

Run from the repository root after building ascii-chat:
  python tests/manual/webrtc_port_forwarding.py --control-url http://ROUTER/CONTROL

Requires a reachable UPnP gateway, STUN, and router NAT loopback support. Only
server-reflexive candidates are signaled (including candidates embedded in SDP).
This verifies router readback and data delivery, not off-LAN reachability.
The ctypes Config/Stun layouts mirror the public C headers; keep them in sync.
"""
import argparse
import sys
import ctypes as c
import os
import time
import queue
import urllib.request
import urllib.error
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

sys.stdout.reconfigure(errors="backslashreplace")
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--library-dir', default='build/bin')
parser.add_argument('--control-url', required=True)
parser.add_argument('--service-type', default='urn:schemas-upnp-org:service:WANIPConnection:2')
parser.add_argument('--stun', default='stun:stun.l.google.com:19302')
args = parser.parse_args()
http = urllib.request.build_opener(urllib.request.ProxyHandler({}))

def mapping(port):
    body = f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body><u:GetSpecificPortMappingEntry xmlns:u="{escape(args.service_type)}"><NewRemoteHost></NewRemoteHost><NewExternalPort>{port}</NewExternalPort><NewProtocol>UDP</NewProtocol></u:GetSpecificPortMappingEntry></s:Body></s:Envelope>'
    request = urllib.request.Request(args.control_url, data=body.encode(), headers={'Content-Type': 'text/xml; charset="utf-8"', 'SOAPAction': f'"{args.service_type}#GetSpecificPortMappingEntry"'})
    try:
        with http.open(request, timeout=5) as response:
            payload = response.read()
    except urllib.error.HTTPError as error:
        payload = error.read()
    return {e.tag.rsplit('}', 1)[-1]: e.text or '' for e in ET.fromstring(payload).iter() if len(e) == 0}
verified_ports = set()
from pathlib import Path
library_dir = Path(args.library_dir).resolve()
dll_path = os.add_dll_directory(str(library_dir))
lib = c.CDLL(str(library_dir / 'asciichat.dll'))
rtc = c.CDLL(str(library_dir / 'datachannel.dll'))
lib.platform_init.restype = c.c_int
assert lib.platform_init() == 0
lib.log_init.argtypes = [c.c_char_p, c.c_int, c.c_bool, c.c_bool]
lib.log_init(str(library_dir / 'webrtc-port-forwarding.log').encode(), 2, True, False)
assert lib.webrtc_init() == 0
P = c.c_void_p
S = c.c_char_p
N = c.c_size_t

class Stun(c.Structure):
    _pack_ = 1
    _fields_ = [('length', c.c_uint8), ('host', c.c_char * 64)]

class Config(c.Structure):
    _fields_ = [('bind', S), ('stun', P), ('stun_count', N), ('turn', P), ('turn_count', N), ('relay', c.c_bool), ('username', S), ('password', S)] + [(n, P) for n in ['state', 'gathering', 'description', 'candidate', 'opened', 'message', 'error', 'user']] + [('mapping', c.c_bool)]
q = queue.Queue()
callbacks = []
candidates = {}
messages = []
dcs = {}
peers = []

def callback(sig, fn):
    cb = c.CFUNCTYPE(None, *sig)(fn)
    callbacks.append(cb)
    return c.cast(cb, P)

def desc(pc, sdp, typ, user):
    q.put(('desc', int(user or 0), sdp, typ))

def cand(pc, cand, mid, user):
    q.put(('cand', int(user or 0), cand, mid))

def opened(dc, user):
    q.put(('open', int(user or 0), dc))

def msg(dc, data, size, user):
    q.put(('msg', int(user or 0), c.string_at(data, size)))
D = callback([P, S, S, P], desc)
C = callback([P, S, S, P], cand)
O = callback([P, P], opened)
M = callback([P, P, N, P], msg)
lib.webrtc_create_peer_connection.argtypes = [c.POINTER(Config), c.POINTER(P)]
lib.webrtc_create_datachannel.argtypes = [P, S, c.POINTER(P)]
lib.webrtc_set_remote_description.argtypes = [P, S, S]
lib.webrtc_add_remote_candidate.argtypes = [P, S, S]
lib.webrtc_datachannel_send.argtypes = [P, S, N]
lib.webrtc_peer_connection_destroy.argtypes = [P]
lib.webrtc_datachannel_destroy.argtypes = [P]
lib.webrtc_get_rtc_id.argtypes = [P]
lib.webrtc_get_rtc_id.restype = c.c_int
rtc.rtcGetSelectedCandidatePair.argtypes = [c.c_int, P, c.c_int, P, c.c_int]
stun = Stun()
stun.host = args.stun.encode()
stun.length = len(stun.host)
configs = []
remote_set = [False, False]
pending = [[], []]
sent = False
try:
    for i in range(2):
        cfg = Config()
        cfg.stun = c.cast(c.pointer(stun), P)
        cfg.stun_count = 1
        cfg.mapping = True
        cfg.description = D
        cfg.candidate = C
        cfg.opened = O
        cfg.message = M
        cfg.user = i
        configs.append(cfg)
        pc = P()
        assert lib.webrtc_create_peer_connection(c.byref(cfg), c.byref(pc)) == 0
        peers.append(pc)
    dc = P()
    assert lib.webrtc_create_datachannel(peers[0], b'upnp-live-test', c.byref(dc)) == 0
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        try:
            e = q.get(timeout=0.2)
        except queue.Empty:
            continue
        typ, i, *data = e
        other = 1 - i
        if typ == 'desc':
            data[0] = b'\r\n'.join((line for line in data[0].split(b'\r\n') if not line.startswith(b'a=candidate:') or b' typ srflx ' in line + b' '))
            assert lib.webrtc_set_remote_description(peers[other], *data) == 0
            remote_set[other] = True
            for item in pending[other]:
                assert lib.webrtc_add_remote_candidate(peers[other], *item) == 0
            pending[other] = []
        elif typ == 'cand':
            print('CANDIDATE', i, data[0].decode(), flush=True)
            candidates.setdefault(i, []).append(data[0].decode())
            if b' typ srflx ' not in data[0] + b' ':
                continue
            words = data[0].decode().split()
            port = int(words[5])
            entry = mapping(port)
            assert entry.get('NewEnabled') == '1' and entry.get('NewPortMappingDescription') == 'ascii-chat WebRTC', entry
            assert int(entry['NewInternalPort']) == port and int(entry['NewLeaseDuration']) > 0, entry
            host_candidates = [x.split() for x in candidates[i] if ' typ host ' in x + ' ']
            assert any((x[4] == entry['NewInternalClient'] and int(x[5]) == port for x in host_candidates)), entry
            verified_ports.add(port)
            print('ROUTER UDP', port, entry, flush=True)
            if remote_set[other]:
                assert lib.webrtc_add_remote_candidate(peers[other], *data) == 0
            else:
                pending[other].append(data)
        elif typ == 'open':
            dcs[i] = data[0]
        elif typ == 'msg':
            messages.append(data[0])
            print('RECEIVED', i, data[0], flush=True)
        if len(dcs) == 2 and (not sent):
            assert lib.webrtc_datachannel_send(dcs[0], b'upnp-live-ping', 14) == 0
            sent = True
        if messages and all((any((' typ srflx ' in x + ' ' for x in candidates.get(j, []))) for j in range(2))):
            break
    assert messages == [b'upnp-live-ping'], 'No data-channel delivery'
    assert all((any((' typ srflx ' in x + ' ' for x in candidates.get(j, []))) for j in range(2))), 'No public STUN candidates'
    for pc in peers:
        local = c.create_string_buffer(1024)
        remote = c.create_string_buffer(1024)
        assert rtc.rtcGetSelectedCandidatePair(lib.webrtc_get_rtc_id(pc), local, 1024, remote, 1024) >= 0
        print('SELECTED', local.value.decode(), remote.value.decode(), flush=True)
    assert len(verified_ports) == 2
    print('PASS: router mappings, public ICE candidates, and native data-channel delivery', flush=True)
finally:
    for dc in set(dcs.values()):
        lib.webrtc_datachannel_destroy(dc)
    for pc in reversed(peers):
        lib.webrtc_peer_connection_destroy(pc)
    lib.webrtc_destroy()
    for port in verified_ports:
        entry = mapping(port)
        assert entry.get('errorCode') == '714', entry
    print('PASS: router confirms peer mappings removed', flush=True)
