#!/bin/bash
# Capture thread backtraces from running ascii-chat process using lldb
# Usage: ./scripts/debug_backtraces.sh [optional_pid]

PID="${1:-$(pgrep -f 'ascii-chat.*server' | head -1)}"

if [ -z "$PID" ]; then
    echo "Error: No ascii-chat process found and no PID provided"
    echo "Usage: $0 [pid]"
    exit 1
fi

echo "Attaching lldb to PID $PID..."
echo ""

# Create temporary lldb command file
LLDB_CMD_FILE=$(mktemp)
cat > "$LLDB_CMD_FILE" << 'EOF'
process attach --pid $PID_PLACEHOLDER
thread list
thread backtrace all
quit
EOF

# Replace placeholder with actual PID
sed -i "s/\$PID_PLACEHOLDER/$PID/g" "$LLDB_CMD_FILE"

# Run lldb with commands
lldb --source "$LLDB_CMD_FILE" 2>&1 | tee /tmp/lldb_backtraces.log

# Clean up
rm -f "$LLDB_CMD_FILE"

echo ""
echo "Full backtrace output saved to: /tmp/lldb_backtraces.log"
