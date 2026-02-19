#!/bin/bash
# Capture and summarize thread states from running ascii-chat process
# Usage: ./scripts/debug_threads_summary.sh [optional_pid]

PID="${1:-$(pgrep -f 'ascii-chat.*server' | head -1)}"

if [ -z "$PID" ]; then
    echo "Error: No ascii-chat process found"
    exit 1
fi

# Create temporary lldb command file
LLDB_CMD_FILE=$(mktemp)
cat > "$LLDB_CMD_FILE" << 'EOF'
process attach --pid $PID_PLACEHOLDER
thread backtrace all
quit
EOF

sed -i "s/\$PID_PLACEHOLDER/$PID/g" "$LLDB_CMD_FILE"

# Run lldb and capture output
BACKTRACE_OUTPUT=$(lldb --source "$LLDB_CMD_FILE" 2>&1)

# Parse and summarize thread states
echo "$BACKTRACE_OUTPUT" | awk '
BEGIN {
    thread_num = 0
    in_backtrace = 0
}

/^  thread #[0-9]+/ {
    thread_num = match($0, /thread #([0-9]+)/, arr) ? arr[1] : "?"
    in_backtrace = 1
    frame_count = 0
    location = "unknown"
    blocked_on = ""
}

in_backtrace && /frame #[0-9]+:/ {
    frame_count++
    $0 = substr($0, index($0, ":") + 2)

    # Extract function name and file:line
    if (match($0, /([^ ]+)\(/, arr)) {
        func = arr[1]
    }
    if (match($0, / at ([^:]+):([0-9]+)/, arr)) {
        file = arr[1]
        line = arr[2]
        location = file ":" line
    }

    # Detect blocking conditions
    if ($0 ~ /pthread_mutex_lock/) blocked_on = "MUTEX_LOCK"
    if ($0 ~ /pthread_cond/) blocked_on = "COND_WAIT"
    if ($0 ~ /usleep|nanosleep/) blocked_on = "SLEEPING"
    if ($0 ~ /select\(/) blocked_on = "SELECT"
    if ($0 ~ /__poll/) blocked_on = "POLL"
    if ($0 ~ /lws_service/) blocked_on = "LWS_EVENT_LOOP"
    if ($0 ~ /websocket_server_callback/) blocked_on = "WEBSOCKET_CALLBACK"
    if ($0 ~ /client_send_thread/) blocked_on = "SEND_THREAD"
    if ($0 ~ /client_video_render/) blocked_on = "RENDER_THREAD"
    if ($0 ~ /client_receive_thread/) blocked_on = "RECV_THREAD"
    if ($0 ~ /client_dispatch_thread/) blocked_on = "DISPATCH_THREAD"
    if ($0 ~ /client_audio_render/) blocked_on = "AUDIO_RENDER"
    if ($0 ~ /crypto_handshake/) blocked_on = "CRYPTO"
}

in_backtrace && /^[^f]/ && !/^$/ && frame_count > 0 {
    in_backtrace = 0
    printf "Thread #%-2d: %s (frames: %d)\n", thread_num, location, frame_count
    if (blocked_on != "") printf "          ⚠ %s\n", blocked_on
}

END {
    if (in_backtrace && frame_count > 0) {
        printf "Thread #%-2d: %s (frames: %d)\n", thread_num, location, frame_count
        if (blocked_on != "") printf "          ⚠ %s\n", blocked_on
    }
}
' | sort -V

# Detailed analysis
echo ""
echo "=== DETAILED THREAD STATES ==="
echo "$BACKTRACE_OUTPUT" | awk '
BEGIN {
    thread_num = 0
}

/^  thread #[0-9]+/ {
    thread_num = match($0, /thread #([0-9]+)/, arr) ? arr[1] : "?"
    getline  # skip status line

    # Get first frame (current location)
    getline frame_line

    printf "Thread #%d: ", thread_num

    # Parse frame location
    if (match(frame_line, / at ([^:]+):([0-9]+)/, arr)) {
        split(arr[1], parts, "/")
        file = parts[length(parts)]
        printf "%s:%s", file, arr[2]
    }

    # Get next line for function
    getline next_line
    if (match(next_line, /frame #[0-9]+: 0x[^ ]+ ([^ \(]+)/, arr)) {
        printf " in %s\n", arr[1]
    } else {
        printf "\n"
    }
}
'

rm -f "$LLDB_CMD_FILE"
