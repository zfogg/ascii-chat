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

    result = subprocess.run(['lldb', '--source', cmd_file], capture_output=True, text=True, timeout=10)
    return result.stdout

def parse_backtraces(output):
    """Parse lldb output into thread info"""
    threads = {}
    current_thread = None
    frame_num = 0

    for line in output.split('\n'):
        # New thread
        if line.startswith('  thread #'):
            match = re.search(r'thread #(\d+).*?name = \'([^\']*)', line)
            if match:
                tid = match.group(1)
                current_thread = {
                    'id': tid,
                    'name': match.group(2),
                    'frames': []
                }
                threads[tid] = current_thread
                frame_num = 0

        # Frame line
        elif current_thread and line.strip().startswith('frame #'):
            frame_num += 1
            # Extract location from frame
            match = re.search(r'at ([^:]+):(\d+)', line)
            if match:
                file = match.group(1).split('/')[-1]
                lineno = match.group(2)
                current_thread['frames'].append({
                    'num': frame_num - 1,
                    'location': f"{file}:{lineno}",
                    'line': line.strip()
                })

    return threads

def categorize_thread(thread):
    """Determine what thread is doing based on stack"""
    if not thread['frames']:
        return "unknown", "no frames"

    # Join all frame lines for analysis
    all_frames = ' '.join([f['line'] for f in thread['frames']])

    # Check for specific patterns
    if 'websocket_server_callback' in all_frames and 'WRITEABLE' in all_frames:
        return "🔴 WebSocket WRITEABLE", "Processing frame queue"
    if 'websocket_server_callback' in all_frames and 'RECEIVE' in all_frames:
        return "📨 WebSocket RECEIVE", "Processing incoming frames"
    if 'websocket_recv' in all_frames:
        return "📥 Recv Thread", "Waiting for WebSocket data"
    if 'client_send_thread' in all_frames:
        return "📤 Send Thread", "Sending video frames"
    if 'client_video_render' in all_frames:
        return "🎨 Video Render", "Rendering ASCII art"
    if 'client_audio_render' in all_frames:
        return "🔊 Audio Render", "Processing audio"
    if 'client_dispatch' in all_frames:
        return "📋 Dispatch Thread", "Routing packets"
    if 'debug_thread_func' in all_frames:
        return "🐛 Debug Thread", "Debug utilities"
    if 'stats_logger' in all_frames:
        return "📊 Stats Logger", "Logging statistics"

    # Check blocking conditions
    if 'usleep' in all_frames or 'nanosleep' in all_frames:
        return "💤 Sleeping", f"usleep: {thread['frames'][0]['location']}"
    if 'pthread_mutex_lock' in all_frames:
        return "🔒 MUTEX BLOCKED", f"Waiting for mutex at {thread['frames'][1]['location']}"
    if 'pthread_cond' in all_frames:
        return "⏳ COND WAIT", f"Waiting for condition at {thread['frames'][1]['location']}"
    if '__select' in all_frames or 'select' in all_frames:
        return "⏱️  Select Poll", "Waiting for socket activity"
    if 'lws_service' in all_frames:
        return "⚡ LWS Event Loop", "WebSocket event loop"

    return "❓ Unknown", thread['frames'][0]['location'] if thread['frames'] else "?"

def main():
    pid = sys.argv[1] if len(sys.argv) > 1 else get_server_pid()

    if not pid:
        print("Error: No ascii-chat server found")
        sys.exit(1)

    print(f"Analyzing threads in PID {pid}...\n")

    backtraces = get_backtraces(pid)
    threads = parse_backtraces(backtraces)

    print("=" * 80)
    print("THREAD STATE SUMMARY")
    print("=" * 80)

    for tid in sorted(threads.keys(), key=lambda x: int(x)):
        thread = threads[tid]
        category, detail = categorize_thread(thread)

        # Show top 2 frames for context
        frames_str = ""
        if thread['frames']:
            for i, frame in enumerate(thread['frames'][:2]):
                if i == 0:
                    frames_str += frame['location']

        print(f"\nThread #{tid:2s}: {category:25s} | {detail:30s}")
        if frames_str:
            print(f"          Location: {frames_str}")

    print("\n" + "=" * 80)

if __name__ == '__main__':
    main()
