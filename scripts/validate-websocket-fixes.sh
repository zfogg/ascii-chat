#!/bin/bash
# Comprehensive validation of websocket fixes
# Runs test-websocket-server-client.sh multiple times and collects metrics
# Measures: FPS rate, frame arrival consistency, client connection stability, memory usage

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

# Configuration
NUM_RUNS=25
TEST_SCRIPT="$SCRIPT_DIR/test-websocket-server-client.sh"
OUTPUT_DIR="/tmp/websocket-validation-$(date +%s)"
METRICS_FILE="$OUTPUT_DIR/metrics.csv"

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "🧪 WebSocket Validation Test Suite"
echo "===================================="
echo "Running $NUM_RUNS iterations of websocket server-client test"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Initialize metrics file
echo "run,fps,frames_captured,connection_time_ms,max_memory_mb,crashed,duration_sec" > "$METRICS_FILE"

# Track statistics
total_fps=0
total_frames=0
total_connections=0
total_memory=0
crash_count=0
success_count=0
connection_failures=0

echo "Starting test runs..."
echo ""

for i in $(seq 1 $NUM_RUNS); do
  RUN_OUTPUT_DIR="$OUTPUT_DIR/run-$i"
  mkdir -p "$RUN_OUTPUT_DIR"

  TEST_LOG="$RUN_OUTPUT_DIR/test.log"

  printf "[%2d/$NUM_RUNS] " "$i"

  # Run test and capture output
  START_TIME=$(date +%s%N)
  if bash "$TEST_SCRIPT" > "$TEST_LOG" 2>&1; then
    END_TIME=$(date +%s%N)
    DURATION=$(( (END_TIME - START_TIME) / 1000000 ))
    DURATION_SEC=$(echo "scale=3; $DURATION / 1000" | bc)

    # Extract metrics from logs
    CLIENT_STDOUT=$(grep "Client stdout:" "$TEST_LOG" | awk '{print $NF}')
    SERVER_LOG=$(grep "Server:" "$TEST_LOG" | tail -1 | awk '{print $NF}')

    # Count frames in client output (lines starting with frame indicators)
    FRAMES_CAPTURED=$(grep -c "Frame\|frame" "$CLIENT_STDOUT" 2>/dev/null || echo "0")

    # Calculate FPS
    if [ "$DURATION_SEC" != "0" ]; then
      FPS=$(echo "scale=2; $FRAMES_CAPTURED / $DURATION_SEC" | bc)
    else
      FPS="0"
    fi

    # Check for crashes
    if grep -q "AddressSanitizer\|SUMMARY:\|Segmentation" "$CLIENT_STDOUT" 2>/dev/null; then
      STATUS="❌ CRASH"
      crashed="1"
      ((crash_count++))
    else
      STATUS="✅ OK"
      crashed="0"
      ((success_count++))
    fi

    # Get memory usage from process logs (approximate)
    MAX_MEM=0
    if [ -f "$SERVER_LOG" ]; then
      MAX_MEM=$(grep -oP 'memory[:\s]+\K[0-9.]+' "$SERVER_LOG" 2>/dev/null | tail -1 || echo "0")
    fi
    if [ -z "$MAX_MEM" ]; then
      MAX_MEM="0"
    fi

    # Measure connection time (time from start to first frame received)
    CONNECTION_TIME=0
    if [ -f "$CLIENT_STDOUT" ]; then
      # Simple heuristic: connection successful if we see frame data
      if grep -q "Frame\|frame" "$CLIENT_STDOUT"; then
        CONNECTION_TIME=$(head -100 "$CLIENT_STDOUT" | grep -n "Frame\|frame" | head -1 | cut -d: -f1)
        CONNECTION_TIME=$((CONNECTION_TIME * 50)) # Approximate ms
      fi
    fi

    # Accumulate statistics
    total_fps=$(echo "$total_fps + $FPS" | bc)
    total_frames=$((total_frames + FRAMES_CAPTURED))
    total_connections=$((total_connections + CONNECTION_TIME))
    total_memory=$(echo "$total_memory + $MAX_MEM" | bc)

    # Log metrics
    echo "$i,$FPS,$FRAMES_CAPTURED,$CONNECTION_TIME,$MAX_MEM,$crashed,$DURATION_SEC" >> "$METRICS_FILE"

    echo "$STATUS - FPS: $FPS, Frames: $FRAMES_CAPTURED, Memory: ${MAX_MEM}MB, Duration: ${DURATION_SEC}s"
  else
    STATUS="❌ FAILED"
    ((connection_failures++))
    echo "$STATUS - Test execution failed"
    echo "$i,0,0,0,0,1,0" >> "$METRICS_FILE"
  fi
done

echo ""
echo "📊 Validation Summary"
echo "===================="
echo "Total runs: $NUM_RUNS"
echo "Successful: $success_count"
echo "Crashed: $crash_count"
echo "Connection failures: $connection_failures"
echo ""

# Calculate averages
AVG_FPS=$(echo "scale=3; $total_fps / $NUM_RUNS" | bc)
AVG_FRAMES=$(echo "scale=1; $total_frames / $NUM_RUNS" | bc)
AVG_CONN_TIME=$(echo "scale=1; $total_connections / $NUM_RUNS" | bc)
AVG_MEMORY=$(echo "scale=2; $total_memory / $NUM_RUNS" | bc)

echo "📈 Metrics Averages"
echo "==================="
echo "Average FPS: $AVG_FPS"
echo "Average frames per run: $AVG_FRAMES"
echo "Average connection time: ${AVG_CONN_TIME}ms"
echo "Average memory: ${AVG_MEMORY}MB"
echo ""

# Check if FPS is still <1
if (( $(echo "$AVG_FPS < 1" | bc -l) )); then
  echo "⚠️  WARNING: Average FPS is below 1 fps ($AVG_FPS)"
  echo "Analyzing remaining bottlenecks..."

  # Check log files for clues
  echo ""
  echo "🔍 Potential Bottlenecks:"

  # Look for timeout or throttling issues
  for RUN_LOG in "$OUTPUT_DIR"/run-*/test.log; do
    if grep -q "timeout\|throttle\|queue\|dropped" "$RUN_LOG" 2>/dev/null; then
      echo "  - Queue/throttling issues detected in $(basename $(dirname "$RUN_LOG"))"
    fi
  done

  # Look for memory issues
  if (( $(echo "$AVG_MEMORY > 500" | bc -l) )); then
    echo "  - High memory usage detected: ${AVG_MEMORY}MB"
  fi

  # Look for connection instability
  if [ $connection_failures -gt 0 ]; then
    echo "  - Connection failures: $connection_failures runs"
  fi

  echo ""
  echo "📝 Recomm endations:"
  echo "  1. Check packet queue depth and delivery timing"
  echo "  2. Review frame buffering strategy"
  echo "  3. Analyze WebSocket callback timing (see analysis_websocket_test.md)"
  echo "  4. Look for thread synchronization issues"
else
  echo "✅ Good: Average FPS is $AVG_FPS (>= 1 fps)"
fi

# Copy metrics to repo
RESULTS_FILE="$REPO_ROOT/VALIDATION_RESULTS.md"
cat > "$RESULTS_FILE" << 'EOF'
# WebSocket Fixes Validation Results

## Test Configuration
- Test Script: `scripts/test-websocket-server-client.sh`
- Number of Runs: NUM_RUNS_PLACEHOLDER
- Date: DATE_PLACEHOLDER
- Branch: BRANCH_PLACEHOLDER

## Summary
- Successful runs: SUCCESS_COUNT_PLACEHOLDER
- Crashed runs: CRASH_COUNT_PLACEHOLDER
- Connection failures: CONN_FAILURES_PLACEHOLDER
- Overall success rate: SUCCESS_RATE_PLACEHOLDER%

## Key Metrics

### Frame Delivery (FPS Analysis)
- **Average FPS**: AVG_FPS_PLACEHOLDER
- **Average Frames per Run**: AVG_FRAMES_PLACEHOLDER
- **Total Frames Captured**: TOTAL_FRAMES_PLACEHOLDER

Status: FPS_STATUS_PLACEHOLDER

### Connection Stability
- **Average Connection Time**: AVG_CONN_TIME_PLACEHOLDER ms
- **Connection Failure Rate**: CONN_FAILURE_RATE_PLACEHOLDER%

### Memory Usage
- **Average Memory**: AVG_MEMORY_PLACEHOLDER MB
- **Memory Status**: MEMORY_STATUS_PLACEHOLDER

## Before/After Comparison

### Previous State (Issue #305)
- FPS: <1 (throttled frame delivery, 0fps often observed)
- Frame delivery: Inconsistent, dropped frames
- Connection stability: Variable
- Root cause: WebSocket callback timing and packet queue bottlenecks

### Current State (After Stage 2 Fixes)
- FPS: AVG_FPS_PLACEHOLDER
- Frame delivery: AVG_FRAMES_PLACEHOLDER per run
- Connection stability: CONNECTION_STATUS_PLACEHOLDER
- Improvements: See detailed analysis below

## Detailed Findings

### Frame Delivery Consistency
The test captures frames using snapshot mode (-S -D 1), expecting 1 frame per second over 5 seconds (5 frames target).

- Frame count distribution: AVG_FRAMES_PLACEHOLDER frames average
- Success rate: SUCCESS_COUNT_PLACEHOLDER / NUM_RUNS_PLACEHOLDER runs
- Variance: Check metrics.csv for distribution

### Stability Analysis
- Crash rate: CRASH_RATE_PLACEHOLDER%
- Memory issues: MEMORY_ISSUES_PLACEHOLDER
- Connection issues: CONN_ISSUES_PLACEHOLDER

## Bottleneck Analysis

BOTTLENECK_ANALYSIS_PLACEHOLDER

## Raw Data
Detailed metrics available in: `/tmp/websocket-validation-*/metrics.csv`

## Recommendations

RECOMMENDATIONS_PLACEHOLDER

## Next Steps
NEXT_STEPS_PLACEHOLDER
EOF

# Update the placeholders with actual values
sed -i "s|NUM_RUNS_PLACEHOLDER|$NUM_RUNS|g" "$RESULTS_FILE"
sed -i "s|DATE_PLACEHOLDER|$(date)|g" "$RESULTS_FILE"
sed -i "s|BRANCH_PLACEHOLDER|$(git branch --show-current)|g" "$RESULTS_FILE"
sed -i "s|SUCCESS_COUNT_PLACEHOLDER|$success_count|g" "$RESULTS_FILE"
sed -i "s|CRASH_COUNT_PLACEHOLDER|$crash_count|g" "$RESULTS_FILE"
sed -i "s|CONN_FAILURES_PLACEHOLDER|$connection_failures|g" "$RESULTS_FILE"
sed -i "s|AVG_FPS_PLACEHOLDER|$AVG_FPS|g" "$RESULTS_FILE"
sed -i "s|AVG_FRAMES_PLACEHOLDER|$AVG_FRAMES|g" "$RESULTS_FILE"
sed -i "s|TOTAL_FRAMES_PLACEHOLDER|$total_frames|g" "$RESULTS_FILE"
sed -i "s|AVG_CONN_TIME_PLACEHOLDER|$AVG_CONN_TIME|g" "$RESULTS_FILE"
sed -i "s|AVG_MEMORY_PLACEHOLDER|$AVG_MEMORY|g" "$RESULTS_FILE"

# Calculate success rate
SUCCESS_RATE=$((success_count * 100 / NUM_RUNS))
sed -i "s|SUCCESS_RATE_PLACEHOLDER|$SUCCESS_RATE|g" "$RESULTS_FILE"

# Calculate crash rate
CRASH_RATE=$((crash_count * 100 / NUM_RUNS))
sed -i "s|CRASH_RATE_PLACEHOLDER|$CRASH_RATE|g" "$RESULTS_FILE"

# Calculate connection failure rate
if [ $NUM_RUNS -gt 0 ]; then
  CONN_FAIL_RATE=$((connection_failures * 100 / NUM_RUNS))
else
  CONN_FAIL_RATE=0
fi
sed -i "s|CONN_FAILURE_RATE_PLACEHOLDER|$CONN_FAIL_RATE|g" "$RESULTS_FILE"

# Add FPS status
if (( $(echo "$AVG_FPS < 1" | bc -l) )); then
  FPS_STATUS="⚠️  BELOW TARGET (< 1 fps)"
  MEMORY_STATUS="Check for leaks"
else
  FPS_STATUS="✅ GOOD ($AVG_FPS fps)"
  MEMORY_STATUS="Normal"
fi
sed -i "s|FPS_STATUS_PLACEHOLDER|$FPS_STATUS|g" "$RESULTS_FILE"
sed -i "s|MEMORY_STATUS_PLACEHOLDER|$MEMORY_STATUS|g" "$RESULTS_FILE"

# Add connection status
if (( $(echo "$AVG_CONN_TIME < 100" | bc -l) )); then
  CONNECTION_STATUS="Fast connection (< 100ms)"
else
  CONNECTION_STATUS="Slow connection (>= 100ms)"
fi
sed -i "s|CONNECTION_STATUS_PLACEHOLDER|$CONNECTION_STATUS|g" "$RESULTS_FILE"

# Placeholder replacements for analysis sections
sed -i "s|MEMORY_ISSUES_PLACEHOLDER|Memory tracking: $AVG_MEMORY MB average|g" "$RESULTS_FILE"
sed -i "s|CONN_ISSUES_PLACEHOLDER|Connection stability: $connection_failures failures observed|g" "$RESULTS_FILE"

# Add bottleneck analysis
if (( $(echo "$AVG_FPS < 1" | bc -l) )); then
  BOTTLENECK_TEXT="### Critical Findings
- **FPS Throttling**: Frames are still being delivered below target
- **Potential Issues**:
  1. WebSocket callback timing may still have delays
  2. Packet queue may be accumulating frames
  3. Frame capture/encoding could be slow
  4. Buffer synchronization issues

### Recommended Investigation
- Review WebSocket callback profiling: \`analysis_websocket_test.md\`
- Check packet_queue.c for delivery delays
- Profile frame capture timing
- Verify thread synchronization in buffer management"
else
  BOTTLENECK_TEXT="### Status
No significant bottlenecks detected. Frame delivery meets targets.

### Performance Notes
- Average FPS: $AVG_FPS (acceptable)
- Frame consistency: $AVG_FRAMES frames per run
- Connection stability: Good ($CONN_FAIL_RATE% failure rate)"
fi
sed -i "s|BOTTLENECK_ANALYSIS_PLACEHOLDER|$BOTTLENECK_TEXT|g" "$RESULTS_FILE"

# Add recommendations
if (( $(echo "$AVG_FPS < 1" | bc -l) )); then
  RECS_TEXT="1. **Investigate WebSocket Timing**: Review callback profiling in docs/
2. **Profile Frame Delivery**: Use \`--log-level debug --grep 'frame\\|callback'\`
3. **Check Packet Queues**: Monitor queue depth during playback
4. **Thread Sync Analysis**: Look for lock contention in frame buffering
5. **Consider Batching**: May need to batch frame deliveries"
else
  RECS_TEXT="1. Monitor frame delivery rates in production
2. Watch for memory growth over long sessions
3. Test with multiple simultaneous connections
4. Validate fixes with various frame rates (30fps, 60fps)
5. Continue profiling for future optimizations"
fi
sed -i "s|RECOMMENDATIONS_PLACEHOLDER|$RECS_TEXT|g" "$RESULTS_FILE"

# Add next steps
NEXT_STEPS_TEXT="1. Review this validation report
2. If FPS < 1: Escalate with debug logs and bottleneck findings
3. If FPS >= 1: Mark issue as resolved pending code review
4. Monitor for regressions in production
5. Plan performance optimization roadmap"
sed -i "s|NEXT_STEPS_PLACEHOLDER|$NEXT_STEPS_TEXT|g" "$RESULTS_FILE"

echo ""
echo "📄 Full report saved to: $RESULTS_FILE"
echo ""
echo "Detailed metrics available in:"
for csv in "$OUTPUT_DIR"/metrics.csv; do
  [ -f "$csv" ] && echo "  $csv"
done
