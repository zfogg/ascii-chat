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
import atexit

PID_FILE = 'ascii-chat-debug.pid'

def cleanup_pid_file():
    """Kill process in PID file on exit"""
    try:
        if os.path.exists(PID_FILE):
            with open(PID_FILE, 'r') as f:
                pid = f.read().strip()
            if pid:
                print(f"\n[Cleanup] Killing process {pid} (kill -9)...")
                subprocess.run(['kill', '-9', pid], timeout=2)
                time.sleep(0.5)
    except Exception as e:
        pass

def kill_existing_processes():
    """Kill any existing ascii-chat servers from previous runs"""
    print("Cleaning up any existing debug processes...")

    # Kill from PID file if it exists
    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE, 'r') as f:
                old_pid = f.read().strip()
            if old_pid:
                print(f"  Killing previous process {old_pid} (kill -9)...")
                subprocess.run(['kill', '-9', old_pid], timeout=2)
                time.sleep(0.5)
        except Exception as e:
            pass

    # Also kill any matching processes
    print("  Killing any other ascii-chat server processes...")
    try:
        subprocess.run(['pkill', '-x', '-9', 'ascii-chat'], timeout=2)
    except Exception as e:
        pass
    time.sleep(1)

def start_server():
    """Start the ascii-chat server (uses webcam by default)"""
    print("Starting ascii-chat server...")
    proc = subprocess.Popen(['./build/bin/ascii-chat', 'server'],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Server started with PID {proc.pid}")

    # Save PID to file for cleanup on next run
    try:
        with open(PID_FILE, 'w') as f:
            f.write(str(proc.pid))
        print(f"Saved PID to: {PID_FILE}")
    except Exception as e:
        print(f"Warning: Could not save PID file: {e}")

    time.sleep(2)  # Wait for server to start

    # Verify server is running by checking if the process still exists
    try:
        os.kill(proc.pid, 0)  # Signal 0 checks if process exists without killing it
        print(f"Server confirmed running with PID {proc.pid}")
    except (OSError, ProcessLookupError):
        print("ERROR: Server failed to start!")
        sys.exit(1)

    time.sleep(3)  # Wait for server to send first frame before capturing backtraces
    return proc.pid


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
    # Register cleanup to run on exit
    atexit.register(cleanup_pid_file)

    pid = sys.argv[1] if len(sys.argv) > 1 else None

    if not pid:
        kill_existing_processes()
        pid = start_server()

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
