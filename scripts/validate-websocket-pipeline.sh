#!/bin/bash
# Validate end-to-end ASCII art pipeline with repeated websocket tests
# Checks for consistent output, data loss, and performance metrics

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

NUM_RUNS="${1:-5}"
TIMESTAMP=$(date +%s)

# Stats tracking
TOTAL_RUNS=0
PASSED_RUNS=0
CRASHED_RUNS=0
DATA_LOSS_RUNS=0
FRAME_COUNTS=()
CALLBACK_TIMES=()

{
    echo "🔄 WebSocket E2E ASCII Art Pipeline Validation"
    echo "=============================================="
    echo "Started: $(date)"
    echo "Running $NUM_RUNS validation cycles..."
    echo ""

    for run in $(seq 1 "$NUM_RUNS"); do
        echo "📍 Run $run/$NUM_RUNS:"
        TOTAL_RUNS=$((TOTAL_RUNS + 1))

        # Remove old temp files to avoid confusion
        rm -f /tmp/ws-test-current-* 2>/dev/null || true

        # Run the test and save output
        bash scripts/test-websocket-server-client.sh > /tmp/ws-test-current-output.log 2>&1 || true

        # Extract paths from the output
        SERVER_LOG=$(grep "Server:" /tmp/ws-test-current-output.log | head -1 | awk '{print $NF}')
        CLIENT_LOG=$(grep "Client:" /tmp/ws-test-current-output.log | head -1 | awk '{print $NF}')
        CLIENT_STDOUT=$(grep "Client stdout:" /tmp/ws-test-current-output.log | head -1 | awk '{print $NF}')

        # If extraction failed, try to find most recent files
        if [ -z "$CLIENT_STDOUT" ] || [ ! -f "$CLIENT_STDOUT" ]; then
            CLIENT_STDOUT=$(ls -t /tmp/ascii-chat-client-stdout-*.txt 2>/dev/null | head -1)
        fi
        if [ -z "$CLIENT_LOG" ] || [ ! -f "$CLIENT_LOG" ]; then
            PORT=$(ls -t /tmp/ascii-chat-client-*.log 2>/dev/null | head -1 | sed 's/.*client-//' | sed 's/.log//')
            CLIENT_LOG="/tmp/ascii-chat-client-$PORT.log"
        fi
        if [ -z "$SERVER_LOG" ] || [ ! -f "$SERVER_LOG" ]; then
            PORT=$(ls -t /tmp/ascii-chat-server-*.log 2>/dev/null | head -1 | sed 's/.*server-//' | sed 's/.log//')
            SERVER_LOG="/tmp/ascii-chat-server-$PORT.log"
        fi

        echo "   Checking: $CLIENT_STDOUT"

        # Check if files exist
        if [ ! -f "$CLIENT_STDOUT" ]; then
            echo "   ❌ No client stdout found"
            continue
        fi

        # Check for crashes
        if grep -q "AddressSanitizer\|SUMMARY:" "$CLIENT_STDOUT" 2>/dev/null; then
            echo "   ❌ CRASH DETECTED"
            CRASHED_RUNS=$((CRASHED_RUNS + 1))
            continue
        fi

        PASSED_RUNS=$((PASSED_RUNS + 1))

        # Count frames sent by client
        FRAMES_SENT=0
        if [ -f "$CLIENT_LOG" ]; then
            FRAMES_SENT=$(grep -c "ACIP_SEND_IMAGE_FRAME:" "$CLIENT_LOG" 2>/dev/null || echo "0")
            FRAMES_SENT=$(printf "%d" "$FRAMES_SENT" 2>/dev/null || echo "0")
            echo "   📤 Frames SENT: $FRAMES_SENT"
            FRAME_COUNTS+=("$FRAMES_SENT")
        fi

        # Count frames received by server
        FRAMES_RECEIVED=0
        if [ -f "$SERVER_LOG" ]; then
            FRAMES_RECEIVED=$(grep -c "IMAGE_FRAME\|Frame received\|acip_handle_image_frame" "$SERVER_LOG" 2>/dev/null || echo "0")
            FRAMES_RECEIVED=$(printf "%d" "$FRAMES_RECEIVED" 2>/dev/null || echo "0")
            echo "   📥 Frames RECEIVED: $FRAMES_RECEIVED"

            if [ "$FRAMES_SENT" -gt 0 ] && [ "$FRAMES_RECEIVED" -eq 0 ]; then
                echo "   ⚠️  MISMATCH: Sent $FRAMES_SENT but received 0!"
            elif [ "$FRAMES_SENT" -gt 0 ] && [ "$FRAMES_RECEIVED" -gt 0 ]; then
                LOSS_PCT=$((100 - (FRAMES_RECEIVED * 100 / FRAMES_SENT)))
                if [ "$LOSS_PCT" -eq 0 ]; then
                    echo "   ✅ All frames received"
                else
                    echo "   ⚠️  Frame loss: $LOSS_PCT% ($FRAMES_RECEIVED/$FRAMES_SENT)"
                fi
            fi
        fi

        # Check for data loss indicators
        if [ -f "$CLIENT_LOG" ]; then
            TIMEOUT_ERRORS=$(grep -c "Fragment reassembly timeout" "$CLIENT_LOG" 2>/dev/null || echo "0")
            if [ "$TIMEOUT_ERRORS" -gt 0 ]; then
                echo "   ⚠️  Fragment timeouts: $TIMEOUT_ERRORS (POTENTIAL DATA LOSS)"
                DATA_LOSS_RUNS=$((DATA_LOSS_RUNS + 1))
            fi
        fi

        # Extract performance metrics
        if [ -f "$SERVER_LOG" ]; then
            # Average callback time from last few callbacks
            AVG_TIME=$(grep "RECEIVE callback took" "$SERVER_LOG" 2>/dev/null | tail -10 | \
                      sed 's/.*took //' | sed 's/ µs.*//' | \
                      awk '{sum += $1; n++} END {if (n > 0) printf "%.1f", sum/n}')
            if [ ! -z "$AVG_TIME" ] && [ "$AVG_TIME" != "0" ]; then
                echo "   ⏱️  Avg callback: ${AVG_TIME}µs"
                CALLBACK_TIMES+=("$AVG_TIME")
            fi
        fi

        echo ""
        sleep 0.2
    done

    echo ""
    echo "📊 VALIDATION SUMMARY"
    echo "===================="
    echo "Total runs: $TOTAL_RUNS"
    echo "✅ Passed: $PASSED_RUNS"
    echo "❌ Crashed: $CRASHED_RUNS"
    echo "⚠️  Data loss (timeout errors): $DATA_LOSS_RUNS"

    # Frame consistency analysis
    if [ ${#FRAME_COUNTS[@]} -gt 0 ]; then
        echo ""
        echo "📈 FRAME CONSISTENCY CHECK:"
        echo "   Frames per run: ${FRAME_COUNTS[@]}"

        MIN_FRAMES="${FRAME_COUNTS[0]}"
        MAX_FRAMES="${FRAME_COUNTS[0]}"
        SUM=0
        for count in "${FRAME_COUNTS[@]}"; do
            SUM=$((SUM + count))
            [ "$count" -lt "$MIN_FRAMES" ] && MIN_FRAMES="$count"
            [ "$count" -gt "$MAX_FRAMES" ] && MAX_FRAMES="$count"
        done
        AVG=$((SUM / ${#FRAME_COUNTS[@]}))

        if [ "$MIN_FRAMES" -eq "$MAX_FRAMES" ]; then
            echo "   ✅ PERFECT CONSISTENCY: All runs sent $MIN_FRAMES frames"
        else
            VARIANCE=$((MAX_FRAMES - MIN_FRAMES))
            echo "   ⚠️  VARIANCE DETECTED: min=$MIN_FRAMES, max=$MAX_FRAMES, avg=$AVG (variance=$VARIANCE)"
        fi
    fi

    # Performance stats
    if [ ${#CALLBACK_TIMES[@]} -gt 0 ]; then
        echo ""
        echo "⚡ WEBSOCKET CALLBACK PERFORMANCE:"
        echo "   Timing per run (microseconds):"
        for i in "${!CALLBACK_TIMES[@]}"; do
            echo "   - Run $((i+1)): ${CALLBACK_TIMES[$i]}µs"
        done
    fi

    echo ""
    echo "✨ Validation complete - $(date)"

} | tee /tmp/websocket-validation-report-$TIMESTAMP.txt

echo ""
echo "📝 Report: /tmp/websocket-validation-report-$TIMESTAMP.txt"
