"""Opt-in Windows test of server and ACDS WebSocket router mappings.

Run from the repository root with --control-url http://ROUTER/CONTROL --lan-ip IP.
Creates temporary TCP mappings, verifies HTTP Upgrade and router readback, then
sends Ctrl+C and checks deletion. Add --connect-host PUBLIC_IP to test NAT loopback.
"""

import argparse
import sys
import base64
import ctypes
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape

sys.stdout.reconfigure(errors="backslashreplace")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', default='build/bin/ascii-chat.exe')
parser.add_argument('--control-url', required=True)
parser.add_argument('--service-type', default='urn:schemas-upnp-org:service:WANIPConnection:2')
parser.add_argument('--lan-ip', required=True)
parser.add_argument('--connect-host')
parser.add_argument('--base-port', type=int, default=39260)
args = parser.parse_args()
http = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def mapping(port):
    body = (
        '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
        f'<u:GetSpecificPortMappingEntry xmlns:u="{escape(args.service_type)}">'
        '<NewRemoteHost></NewRemoteHost>'
        f'<NewExternalPort>{port}</NewExternalPort><NewProtocol>TCP</NewProtocol>'
        '</u:GetSpecificPortMappingEntry></s:Body></s:Envelope>'
    )
    request = urllib.request.Request(args.control_url, data=body.encode(), headers={
        'Content-Type': 'text/xml; charset="utf-8"',
        'SOAPAction': f'"{args.service_type}#GetSpecificPortMappingEntry"',
    })
    try:
        with http.open(request, timeout=5) as response:
            payload = response.read()
    except urllib.error.HTTPError as error:
        payload = error.read()
    return {e.tag.rsplit('}', 1)[-1]: e.text or '' for e in ET.fromstring(payload).iter() if len(e) == 0}


def stop(process):
    if process.poll() is None:
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.FreeConsole()
        assert kernel.AttachConsole(process.pid)
        kernel.SetConsoleCtrlHandler(None, True)
        try:
            assert kernel.GenerateConsoleCtrlEvent(0, 0)
            time.sleep(.2)
        finally:
            kernel.FreeConsole()
            kernel.SetConsoleCtrlHandler(None, False)
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
            raise
    assert process.returncode == 0, process.returncode


for index, mode in enumerate(['server', 'discovery-service']):
    port = args.base_port + index * 2
    wsport = port + 1
    for candidate in [port, wsport]:
        assert mapping(candidate).get('errorCode') == '714', 'Choose unused test ports'
    with tempfile.TemporaryDirectory(prefix='ascii-chat-upnp-') as directory:
        log_path = Path(directory) / 'server.log'
        command = [str(Path(args.binary).resolve()), mode, '0.0.0.0', '--port', str(port),
                   '--websocket-port', str(wsport), '--port-forwarding', '--status-screen=false']
        if mode == 'discovery-service':
            command += ['--database', str(Path(directory) / 'acds.db')]
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        with log_path.open('wb') as log:
            process = subprocess.Popen(command, stdout=log, stderr=log,
                                       creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline and process.poll() is None:
                    if 'Server accepting connections' in log_path.read_text(errors='replace'):
                        break
                    time.sleep(.2)
                assert 'Server accepting connections' in log_path.read_text(errors='replace'), log_path.read_text(errors='replace')
                for candidate in [port, wsport]:
                    entry = mapping(candidate)
                    assert entry.get('NewEnabled') == '1' and entry.get('NewInternalClient') == args.lan_ip, entry
                    assert int(entry['NewInternalPort']) == candidate and int(entry['NewLeaseDuration']) > 0, entry
                    if candidate == wsport:
                        assert entry['NewPortMappingDescription'] == 'ascii-chat WebSocket', entry
                    print(mode, 'ROUTER TCP', candidate, entry, flush=True)
                host = args.connect_host or args.lan_ip
                with socket.create_connection((host, wsport), timeout=5) as connection:
                    key = base64.b64encode(os.urandom(16)).decode()
                    request = (f'GET / HTTP/1.1\r\nHost: {host}:{wsport}\r\nUpgrade: websocket\r\n'
                               'Connection: Upgrade\r\n'
                               f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n'
                               'Sec-WebSocket-Protocol: acip\r\n\r\n')
                    connection.sendall(request.encode())
                    reply = b''
                    while b'\r\n\r\n' not in reply and len(reply) < 16384:
                        chunk = connection.recv(4096)
                        assert chunk, reply
                        reply += chunk
                    assert b'101 Switching Protocols' in reply, reply
                print(mode, 'PASS: WebSocket HTTP Upgrade', flush=True)
            except BaseException:
                print(log_path.read_text(errors='replace'), flush=True)
                raise
            finally:
                stop(process)
                for candidate in [port, wsport]:
                    assert mapping(candidate).get('errorCode') == '714', candidate
                print(mode, 'PASS: clean shutdown and router mapping removal', flush=True)
