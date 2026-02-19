#!/usr/bin/env python3
import subprocess
import sys
import re
import os

def get_server_pid():
    """Find ascii-chat server PID"""
    result = subprocess.run(['pgrep', '-f', 'ascii-chat.*server'], capture_output=True, text=True)
    pids = result.stdout.strip().split('\n')
    return pids[0] if pids and pids[0] else None

def get_backtraces(pid):
    """Use lldb to get backtraces"""
    cmd_file = '/tmp/lldb_commands.txt'
    with open(cmd_file, 'w') as f:
        f.write(f"""process attach --pid {pid}
thread backtrace all
quit
""")

    result = subprocess.run(['sudo', 'lldb', '--source', cmd_file], capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        print(f"DEBUG: lldb return code: {result.returncode}", file=sys.stderr)
        print(f"DEBUG: lldb stderr: {result.stderr[:500]}", file=sys.stderr)
    return result.stdout

def main():
    pid = sys.argv[1] if len(sys.argv) > 1 else get_server_pid()

    if not pid:
        print("Error: No ascii-chat server found")
        sys.exit(1)

    print(f"Capturing backtraces from PID {pid}...\n")

    backtraces = get_backtraces(pid)

    # Print only the backtrace section (skip setup noise)
    in_backtraces = False
    frame_count = 0
    current_thread = None

    for line in backtraces.split('\n'):
        # Start of backtrace section
        if 'thread backtrace all' in line:
            in_backtraces = True
            continue

        if not in_backtraces:
            continue

        # New thread marker
        if line.startswith('* thread #'):
            current_thread = line
            frame_count = 0
            print("\n" + "="*100)
            print(line)

        # Frame lines
        elif line.startswith('    frame #'):
            frame_count += 1
            # Only print first 10 frames to keep output manageable
            if frame_count <= 10:
                print(line)

        # Empty line between threads
        elif line.strip() == '' and current_thread:
            pass
        else:
            if line.strip() and in_backtraces and not line.startswith('(lldb)'):
                if frame_count <= 10:
                    print(line)

if __name__ == '__main__':
    main()
