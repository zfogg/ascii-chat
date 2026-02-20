#!/usr/bin/env python3
"""
Capture full backtraces from all threads using lldb.
Shows exactly where each thread is blocked/executing.
Automatically starts server and WebSocket client for testing.
"""
import subprocess
import sys
import re
import time
import os
import socket
import threading

def kill_existing_processes():
    """Kill any existing ascii-chat servers (exact match only)"""
    print("Killing existing ascii-chat servers...")
    try:
        subprocess.run(['pkill', '-x', '-9', 'ascii-chat'], timeout=2)
    except Exception as e:
        pass
    time.sleep(1)

def start_server():
    """Start the ascii-chat server (uses webcam by default)"""
    print("Starting ascii-chat server...")
    proc = subprocess.Popen(['/home/linuxbrew/.linuxbrew/bin/ascii-chat', 'server'],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Server started with PID {proc.pid}")
    time.sleep(2)  # Wait for server to start

    # Verify server is running
    pid = get_server_pid()
    if not pid:
        print("ERROR: Server failed to start!")
        sys.exit(1)
    print(f"Server confirmed running with PID {pid}")
    time.sleep(3)  # Wait for server to send first frame before capturing backtraces


def get_server_pid():
    result = subprocess.run(['pgrep', '-f', 'ascii-chat.*server'], capture_output=True, text=True)
    pids = result.stdout.strip().split('\n')
    return pids[0] if pids and pids[0] else None

def get_backtraces(pid):
    cmd_file = '/tmp/lldb_bt.txt'
    with open(cmd_file, 'w') as f:
        f.write(f"""process attach --pid {pid}
thread backtrace all
quit
""")
    result = subprocess.run(['lldb', '--source', cmd_file], capture_output=True, text=True, timeout=20)
    return result.stdout

def main():
    pid = sys.argv[1] if len(sys.argv) > 1 else None

    if not pid:
        kill_existing_processes()
        start_server()
        pid = get_server_pid()

    if not pid:
        print("Error: No ascii-chat server found")
        sys.exit(1)

    print(f"=" * 100)
    print(f"FULL THREAD BACKTRACES - PID {pid}")
    print(f"=" * 100)
    print()

    backtraces = get_backtraces(pid)

    # Parse and print full backtraces
    current_thread = None
    in_backtrace = False
    frame_num = 0

    for line in backtraces.split('\n'):
        # Thread header
        if line.startswith('* thread #') or line.startswith('  thread #'):
            if current_thread is not None:
                print()  # Blank line between threads
            current_thread = line.strip()
            frame_num = 0
            in_backtrace = True
            print(f"\n{'='*100}")
            print(current_thread)
            print(f"{'='*100}")

        # Frame lines
        elif in_backtrace and line.strip().startswith('frame #'):
            print(line)
            frame_num += 1

        # Code lines (assembly, source info)
        elif in_backtrace and line.strip() and not line.startswith('(lldb)') and not 'Executable binary' in line:
            if frame_num > 0:  # Only print if we're in a frame
                print(line)

        # Stop on next thread or end
        elif in_backtrace and line.startswith('(lldb)'):
            in_backtrace = False

if __name__ == '__main__':
    main()
