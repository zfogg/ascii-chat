#!/bin/bash
# WebSocket Callback Profiling Script
# Runs test-websocket-server-client.sh multiple times, collects logs, and analyzes callback performance

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROFILES_DIR="/tmp/ws-callback-profiles-$$"
RESULTS_DIR="$REPO_ROOT"

# Colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  WebSocket Callback Profiling${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"

# Create profiles directory
mkdir -p "$PROFILES_DIR"
echo -e "${GREEN}✓ Profile directory: $PROFILES_DIR${NC}"

# Run tests multiple times
NUM_RUNS=15
echo -e "${YELLOW}Running test-websocket-server-client.sh $NUM_RUNS times...${NC}"

for i in $(seq 1 $NUM_RUNS); do
    echo -e "${BLUE}[Run $i/$NUM_RUNS]${NC} Starting test..."
    cd "$REPO_ROOT"

    # Run test and capture output
    ./scripts/test-websocket-server-client.sh > "$PROFILES_DIR/run_$i.txt" 2>&1 || true

    # Extract server and client logs if they exist
    find /tmp -maxdepth 1 -name "ascii-chat-server-*.log" -newer "$PROFILES_DIR/run_$i.txt" 2>/dev/null | head -1 | xargs -I {} cp {} "$PROFILES_DIR/server_$i.log" 2>/dev/null || true
    find /tmp -maxdepth 1 -name "ascii-chat-client-*.log" -newer "$PROFILES_DIR/run_$i.txt" 2>/dev/null | head -1 | xargs -I {} cp {} "$PROFILES_DIR/client_$i.log" 2>/dev/null || true

    sleep 0.5
done

echo -e "${GREEN}✓ All tests completed${NC}"
echo ""

# Parse logs and extract metrics
echo -e "${BLUE}Parsing logs and extracting metrics...${NC}"

STATS_FILE="$PROFILES_DIR/callback_stats.txt"
: > "$STATS_FILE"

echo "WebSocket Callback Analysis" > "$STATS_FILE"
echo "===========================" >> "$STATS_FILE"
echo "" >> "$STATS_FILE"

# Extract callback counts and timing from server logs
total_receive_callbacks=0
total_writeable_callbacks=0
total_established=0
total_closed=0
declare -a callback_durations_us

for log in "$PROFILES_DIR"/server_*.log; do
    if [ ! -f "$log" ]; then
        continue
    fi

    # Count callback invocations
    receive_count=$(grep -c "LWS_CALLBACK_RECEIVE" "$log" 2>/dev/null || echo "0")
    writeable_count=$(grep -c "LWS_CALLBACK_SERVER_WRITEABLE" "$log" 2>/dev/null || echo "0")
    established_count=$(grep -c "LWS_CALLBACK_ESTABLISHED" "$log" 2>/dev/null || echo "0")
    closed_count=$(grep -c "LWS_CALLBACK_CLOSED" "$log" 2>/dev/null || echo "0")

    total_receive_callbacks=$((total_receive_callbacks + receive_count))
    total_writeable_callbacks=$((total_writeable_callbacks + writeable_count))
    total_established=$((total_established + established_count))
    total_closed=$((total_closed + closed_count))

    # Extract timing data from callback duration logs
    grep "RECEIVE_CALLBACK_DURATION\|CALLBACK_DURATION" "$log" 2>/dev/null | while read -r line; do
        dur_us=$(echo "$line" | grep -oP '(?<=took )[0-9.]+(?= µs)' || echo "0")
        if [ ! -z "$dur_us" ] && [ "$dur_us" != "0" ]; then
            echo "$dur_us" >> "$PROFILES_DIR/durations.txt"
        fi
    done
done

# Calculate statistics
echo "Run Results:" >> "$STATS_FILE"
echo "  Total RECEIVE callbacks: $total_receive_callbacks" >> "$STATS_FILE"
echo "  Total WRITEABLE callbacks: $total_writeable_callbacks" >> "$STATS_FILE"
echo "  Total ESTABLISHED: $total_established" >> "$STATS_FILE"
echo "  Total CLOSED: $total_closed" >> "$STATS_FILE"
echo "  Runs completed: $NUM_RUNS" >> "$STATS_FILE"
echo "" >> "$STATS_FILE"

# Calculate callback frequency (callbacks per second)
if [ $total_receive_callbacks -gt 0 ]; then
    total_test_duration_sec=$((NUM_RUNS * 5)) # Approximate: 5 seconds per test
    callback_frequency=$(awk "BEGIN {printf \"%.2f\", $((total_receive_callbacks + total_writeable_callbacks)) / $total_test_duration_sec}")
    echo "Callback Frequency: $callback_frequency callbacks/second" >> "$STATS_FILE"
fi

echo "" >> "$STATS_FILE"
echo "Timing Analysis:" >> "$STATS_FILE"

# Parse detailed callback timing
if [ -f "$PROFILES_DIR/durations.txt" ]; then
    min_duration=$(sort -n "$PROFILES_DIR/durations.txt" | head -1)
    max_duration=$(sort -nr "$PROFILES_DIR/durations.txt" | head -1)
    avg_duration=$(awk '{sum+=$1; count++} END {if (count > 0) print sum/count; else print 0}' "$PROFILES_DIR/durations.txt")
    median_duration=$(sort -n "$PROFILES_DIR/durations.txt" | awk '{arr[NR]=$0} END {if (NR%2) print arr[NR/2+0.5]; else print (arr[NR/2]+arr[NR/2+1])/2}')

    echo "  Min callback duration: $min_duration µs" >> "$STATS_FILE"
    echo "  Max callback duration: $max_duration µs" >> "$STATS_FILE"
    echo "  Avg callback duration: $avg_duration µs" >> "$STATS_FILE"
    echo "  Median callback duration: $median_duration µs" >> "$STATS_FILE"
fi

echo "" >> "$STATS_FILE"
echo "Queue Analysis:" >> "$STATS_FILE"

# Extract queue depth information from logs
queue_depths=$(grep -oh '\[WS_FLOW\].*free=[0-9]*' "$PROFILES_DIR"/server_*.log 2>/dev/null | grep -oP '(?<=free=)\d+' | sort -n)
if [ ! -z "$queue_depths" ]; then
    min_queue_free=$(echo "$queue_depths" | head -1)
    max_queue_free=$(echo "$queue_depths" | tail -1)
    avg_queue_free=$(echo "$queue_depths" | awk '{sum+=$1; count++} END {print sum/count}')

    echo "  Min queue free space: $min_queue_free bytes" >> "$STATS_FILE"
    echo "  Max queue free space: $max_queue_free bytes" >> "$STATS_FILE"
    echo "  Avg queue free space: $avg_queue_free bytes" >> "$STATS_FILE"
fi

# Display results
cat "$STATS_FILE"

# Copy statistics to repo root for review
cp "$STATS_FILE" "$RESULTS_DIR/ws_callback_stats.txt"
echo -e "${GREEN}✓ Statistics saved to: ws_callback_stats.txt${NC}"

# Summary
echo ""
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo -e "${GREEN}Profiling Summary:${NC}"
echo -e "  Runs completed: $NUM_RUNS"
echo -e "  Total callbacks: $((total_receive_callbacks + total_writeable_callbacks))"
echo -e "  Profiles directory: $PROFILES_DIR"
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
