#!/usr/bin/env python3
"""
Debug ascii-chat server while running E2E tests.

Captures full backtraces from all threads while the E2E test is running.
Starts server on a random port, runs the E2E test against it, takes backtraces
at a specific point (after client connects), then cleans up.
"""
import subprocess
import sys
import time
import os
import socket
import atexit
import random
import threading

PID_FILE = 'ascii-chat-debug-e2e.pid'
TEST_PID_FILE = 'ascii-chat-e2e-test.pid'

def get_random_port():
    """Get a random available port"""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('', 0))
        s.listen(1)
        port = s.getsockname()[1]
    return port

def cleanup_pid_file():
    """Kill process in PID file on exit"""
    try:
        if os.path.exists(PID_FILE):
            with open(PID_FILE, 'r') as f:
                pid = f.read().strip()
            if pid:
                print(f"\n[Cleanup] Killing server process {pid} (kill -9)...")
                subprocess.run(['kill', '-9', pid], timeout=2)
                time.sleep(0.5)
    except Exception as e:
        pass

def cleanup_test_pid_file():
    """Kill E2E test process"""
    try:
        if os.path.exists(TEST_PID_FILE):
            with open(TEST_PID_FILE, 'r') as f:
                pid = f.read().strip()
            if pid:
                print(f"[Cleanup] Killing E2E test process {pid} (kill -9)...")
                subprocess.run(['kill', '-9', pid], timeout=2)
                time.sleep(0.5)
    except Exception as e:
        pass

def kill_existing_processes():
    """Kill any existing ascii-chat servers from previous runs"""
    print("Cleaning up any existing debug processes...")

    # Kill from PID files if they exist
    if os.path.exists(PID_FILE):
        try:
            with open(PID_FILE, 'r') as f:
                old_pid = f.read().strip()
            if old_pid:
                print(f"  Killing previous server process {old_pid} (kill -9)...")
                try:
                    subprocess.run(['kill', '-9', old_pid], timeout=2,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
                except Exception as e:
                    pass
                time.sleep(0.5)
        except Exception as e:
            pass

    if os.path.exists(TEST_PID_FILE):
        try:
            with open(TEST_PID_FILE, 'r') as f:
                old_pid = f.read().strip()
            if old_pid:
                print(f"  Killing previous test process {old_pid} (kill -9)...")
                try:
                    subprocess.run(['kill', '-9', old_pid], timeout=2,
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL)
                except Exception as e:
                    pass
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

def start_server(port):
    """Start the ascii-chat server on specified WebSocket port"""
    print(f"Starting ascii-chat server on WebSocket port {port}...")
    proc = subprocess.Popen(['./build/bin/ascii-chat', 'server', '--websocket-port', str(port)],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"Server started with PID {proc.pid}")

    # Save PID to file for cleanup on next run
    try:
        with open(PID_FILE, 'w') as f:
            f.write(str(proc.pid))
        print(f"Saved server PID to: {PID_FILE}")
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

    time.sleep(2)  # Wait for server to be ready
    return proc.pid

def wait_for_test_connection(test_proc, connection_event, timeout_seconds=30):
    """Wait for E2E test to log that it connected to the server"""
    print(f"\nWaiting for client to connect and complete handshake (max {timeout_seconds}s)...")

    # Wait for connection event or timeout
    if connection_event.wait(timeout=timeout_seconds):
        print("✓ Client connected and handshake detected!")
        return True
    else:
        # Check if process is still alive
        if test_proc.poll() is not None:
            print("✗ E2E test process ended before connecting!")
            return False

        print(f"⚠ Timeout waiting for connection confirmation (waited {timeout_seconds}s)")
        print("⚠ Taking backtraces anyway (may not be mid-connection)...")
        return True

def get_backtraces(pid):
    """Get backtraces from the process using lldb"""
    cmd_file = '/tmp/lldb_bt_e2e.txt'
    with open(cmd_file, 'w') as f:
        f.write(f"""process attach --pid {pid}
thread backtrace all
quit
""")
    result = subprocess.run(['lldb', '--source', cmd_file], capture_output=True, text=True, timeout=20)
    return result.stdout

def start_e2e_test(port):
    """Start the E2E test and monitor for connection"""
    print(f"\nStarting E2E test on port {port}...")
    env = os.environ.copy()
    env['PORT'] = str(port)

    proc = subprocess.Popen(['bun', 'run', 'test:e2e', '--reporter=list'],
                     cwd='web/web.ascii-chat.com',
                     env=env,
                     stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT,
                     text=True,
                     bufsize=1)

    try:
        with open(TEST_PID_FILE, 'w') as f:
            f.write(str(proc.pid))
        print(f"E2E test started with PID {proc.pid}")
    except Exception as e:
        print(f"Warning: Could not save test PID file: {e}")

    # Shared state for connection detection
    connection_event = threading.Event()

    # Thread to read and display test output, watch for connection completion
    def read_test_output():
        try:
            for line in iter(proc.stdout.readline, ''):
                if line:
                    line_stripped = line.rstrip()
                    print(f"[TEST] {line_stripped}")
                    # Only set event when we see "Connected" (completion), not "handshake" (in-progress)
                    # Handshake happens in states 0-2, Connected is the final state
                    if 'Connected' in line_stripped:
                        print(f"[TEST] ✓ Handshake COMPLETE - detected 'Connected' state")
                        connection_event.set()
        except:
            pass

    output_thread = threading.Thread(target=read_test_output, daemon=True)
    output_thread.start()

    return proc, connection_event

def main():
    # Register cleanup to run on exit
    atexit.register(cleanup_pid_file)
    atexit.register(cleanup_test_pid_file)

    # Get a random port
    port = get_random_port()
    print(f"Using random port: {port}")

    # Kill existing processes
    kill_existing_processes()

    # Start server
    server_pid = start_server(port)

    # Start E2E test
    test_proc, connection_event = start_e2e_test(port)

    # Wait for the test to connect and establish connection
    wait_for_test_connection(test_proc, connection_event, timeout_seconds=30)

    # Wait 1s after handshake completes for threads to settle into steady state
    print("\n[Waiting 1s for threads to settle after handshake completion...]")
    time.sleep(1)

    # Now take backtraces while test is running
    print(f"\n{'=' * 100}")
    print(f"FULL THREAD BACKTRACES - SERVER PID {server_pid} (E2E Test Running)")
    print(f"{'=' * 100}")
    print()

    backtraces = get_backtraces(server_pid)

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

    # Give test a moment more, then clean up
    print(f"\n{'=' * 100}")
    print("Backtraces captured. Cleaning up...")
    print(f"{'=' * 100}")

    time.sleep(1)

    # Kill test process
    try:
        test_proc.terminate()
        test_proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        test_proc.kill()
        test_proc.wait()
    except Exception as e:
        pass

if __name__ == '__main__':
    main()
