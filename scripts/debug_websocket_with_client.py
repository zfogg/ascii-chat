#!/usr/bin/env python3
"""
Debug WebSocket frame delivery with actual browser client connection.
Starts server, waits for client to connect via browser, then captures thread backtraces.
"""
import subprocess
import sys
import time
import os
import webbrowser
import threading

PID_FILE = 'ascii-chat-debug.pid'

def cleanup_pid_file():
    """Kill process in PID file on exit"""
    try:
        if os.path.exists(PID_FILE):
            with open(PID_FILE, 'r') as f:
                pid = f.read().strip()
            if pid:
                print(f"\n[Cleanup] Killing process {pid}...")
                subprocess.run(['kill', '-9', pid], timeout=2)
                time.sleep(0.5)
    except Exception:
        pass

def start_server():
    """Start the ascii-chat server"""
    print("Starting ascii-chat server...")
    proc = subprocess.Popen(['./build/bin/ascii-chat', 'server'],
                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    print(f"Server started with PID {proc.pid}")

    try:
        with open(PID_FILE, 'w') as f:
            f.write(str(proc.pid))
    except Exception as e:
        print(f"Warning: Could not save PID file: {e}")

    time.sleep(2)
    return proc

def open_browser():
    """Open browser to localhost:27226 (WebSocket server port)"""
    print("Opening browser to http://localhost:27226...")
    try:
        webbrowser.open('http://localhost:27226', new=1)
    except Exception as e:
        print(f"Warning: Could not open browser: {e}")
        print("Please manually navigate to http://localhost:27226")

def get_backtraces(pid):
    """Capture thread backtraces using lldb"""
    cmd_file = '/tmp/lldb_bt.txt'
    with open(cmd_file, 'w') as f:
        f.write(f"""process attach --pid {pid}
thread backtrace all
quit
""")
    try:
        result = subprocess.run(['lldb', '--source', cmd_file],
                              capture_output=True, text=True, timeout=20)
        return result.stdout
    except Exception as e:
        print(f"Error running lldb: {e}")
        return None

def main():
    print("=" * 100)
    print("WebSocket FPS Bug Debugger with Client Connection")
    print("=" * 100)

    proc = start_server()

    # Open browser in background thread
    browser_thread = threading.Thread(target=open_browser, daemon=True)
    browser_thread.start()

    # Wait for user to connect and interact
    print("\nWaiting 15 seconds for you to:")
    print("1. See the browser load at http://localhost:27226")
    print("2. Click 'Join as WebSocket Client' or similar")
    print("3. Observe frame rendering (or lack thereof)")
    print("\nThen I'll capture thread backtraces to identify bottlenecks...")

    for i in range(15, 0, -1):
        print(f"  {i}s remaining", end='\r')
        time.sleep(1)
    print("\nCapturing thread backtraces...             ")

    backtraces = get_backtraces(proc.pid)
    if backtraces:
        print("\n" + "=" * 100)
        print("THREAD BACKTRACES")
        print("=" * 100)
        print(backtraces)
    else:
        print("Failed to capture backtraces with lldb")

    cleanup_pid_file()
    proc.terminate()

if __name__ == '__main__':
    main()
