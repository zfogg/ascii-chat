# WebSocket FPS Regression Test

## Overview

The WebSocket FPS regression test (`scripts/test-websocket-fps-regression.sh`) detects performance regressions in frame delivery over WebSocket connections. This addresses **Issue #305: Frame Delivery Throttling (0fps issue)**.

## Problem Statement

Previous WebSocket frame delivery had a critical issue where frames would be delivered at 0fps (complete throttling), making video chat unusable. This test prevents regression of that bug.

## Test Behavior

The regression test:

1. **Runs the WebSocket server-client test** (`test-websocket-server-client.sh`)
   - Starts an ascii-chat server with WebSocket support on a random port
   - Connects a client to the server
   - Captures video frames for ~5 seconds
   - Logs all protocol and FPS metrics

2. **Analyzes FPS metrics** from client logs
   - Extracts "actual fps" measurements from capture lag events
   - Checks for critical throttling (0fps, queue full, etc.)
   - Compares against configured baseline threshold

3. **Compares against baseline**
   - Minimum FPS baseline: **1 fps** (very conservative)
   - Fails if any measured FPS falls below baseline
   - Indicates frame delivery failure/regression

4. **Handles three scenarios**:
   - ✅ **Smooth operation**: Protocol initialized, no lag events → PASS (no throttling)
   - ⚠️ **Lag detected**: Protocol initialized, FPS lag events logged but above baseline → PASS
   - ❌ **Regression detected**: FPS below baseline or 0fps → FAIL

## Baseline Metrics

### Current Baseline (as-3v1da)

| Metric | Value | Rationale |
|--------|-------|-----------|
| Minimum FPS | 1 fps | Catches critical throttling (0fps issue) |
| Test duration | 5 seconds | Enough time to establish connection and capture frames |
| Transport | WebSocket | Tests the specific delivery mechanism for remote clients |

### Why Conservative?

The 1 fps baseline is intentionally very conservative to:
- Catch complete frame delivery failure (0fps throttling)
- Detect severe performance regressions quickly
- Allow development improvements to raise the baseline gradually
- Avoid flaky tests due to system resource variations

## Updating Baseline Metrics

### When to Update

Update the baseline when:
1. Performance improvements are implemented and verified
2. Test environment changes (e.g., CI runner upgrade)
3. Measured FPS consistently exceeds current baseline in production

### How to Update

#### 1. Measure Current Performance

Run the test multiple times to establish consistent measurements:

```bash
# Run test 5 times and collect FPS values
for i in {1..5}; do
  bash scripts/test-websocket-fps-regression.sh
  grep "actual fps" /tmp/ascii-chat-client-*.log | tail -1
done
```

#### 2. Calculate New Baseline

Use the **minimum observed value** with 10% safety margin:

```bash
# If observed FPS values are: 15.2, 18.1, 14.8, 16.5, 15.9
# Minimum: 14.8
# Baseline (14.8 * 0.9 = 13.32, round down): 13.0 fps
```

#### 3. Update the Script

Edit `scripts/test-websocket-fps-regression.sh`:

```bash
# Current (line 24):
BASELINE_FPS=1

# Updated example (if improvement verified):
BASELINE_FPS=10
```

#### 4. Commit the Change

```bash
git add scripts/test-websocket-fps-regression.sh
git commit -m "test: Update WebSocket FPS baseline to 10 fps (as-XXXXX)

Improved from 1 fps after fix for:
- Reduce frame queue blocking (improved from 14.8 fps measured)
- Apply 10% safety margin (14.8 * 0.9 = 13.32 → baseline 10 fps)
- Verified across 5 test runs: 15.2, 18.1, 14.8, 16.5, 15.9 fps"
```

## Running Locally

### Quick Test

```bash
bash scripts/test-websocket-fps-regression.sh
```

### With Custom Baseline

```bash
# Override baseline to 5 fps for testing
bash scripts/test-websocket-fps-regression.sh 5
```

### Debug Mode

```bash
# Show detailed logs
bash scripts/test-websocket-fps-regression.sh
# Then examine the logs:
cat /tmp/ascii-chat-client-*.log | grep "actual fps\|FRAME_RECV"
```

## CI Integration

The test runs in GitHub Actions on:
- **Push to master/main** with protocol changes
- **Pull requests** affecting WebSocket/protocol code
- **Daily schedule** (2 AM UTC) to catch environmental regressions
- **Manual trigger** via `workflow_dispatch`

## Interpreting Test Results

### ✅ PASS: "Test completed (smooth operation, no throttling detected)"

- Protocol initialized successfully
- No FPS lag events recorded (smooth frame capture)
- No throttling detected
- **Action**: No issue, proceed with code review

### ⚠️ Conditional PASS: "Measured FPS values during test: X.XX, Y.YY, ..."

- Protocol initialized
- FPS lag events recorded but above baseline
- **Action**: Acceptable for merge, consider baseline update if consistent

### ❌ FAIL: "REGRESSION DETECTED!"

- FPS below baseline or 0fps throttling detected
- **Action**:
  1. Investigate WebSocket protocol changes
  2. Check frame queue implementation
  3. Profile CPU/memory during test
  4. Look for new blocking calls

## Related Issues

- **Issue #305**: Frame Delivery Throttling (0fps issue)
- **PR #342**: WebSocket callback profiling and efficiency analysis
- **Docs**: See `docs/crypto.md` for protocol details

## Troubleshooting

### Test times out

- Check if ascii-chat binary built successfully
- Verify WebSocket port binding works (no firewall blocks)
- Check system resources (disk space for logs)

### No FPS measurements found

- This is normal if video capture is smooth (no lag events logged)
- Check logs manually: `grep "actual fps" /tmp/ascii-chat-client-*.log`

### Intermittent failures

- System under heavy load may cause temporary FPS drops
- Re-run test multiple times to verify
- Consider system resource constraints in CI environment

## Future Improvements

- [ ] Extract frame delivery count from protocol logs as secondary metric
- [ ] Add bandwidth measurement (bytes/frame)
- [ ] Track per-packet latency distribution
- [ ] Integration with performance dashboard for trend analysis
- [ ] Automated baseline updates based on historical data
