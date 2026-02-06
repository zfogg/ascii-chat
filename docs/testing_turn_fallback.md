# TURN Fallback Testing Guide

## Overview

TURN (Traversal Using Relays around NAT) is critical for users behind symmetric NAT (8-15% of users). This guide explains how to test TURN relay functionality.

## Quick Test (Manual)

### Setup Requirements
1. **TURN Server**: Set up coturn or use a public TURN service
2. **Credentials**: Username and password for TURN authentication
3. **Two Machines**: Or two network interfaces to simulate NAT

### Test Command
```bash
# Force TURN-only (skip host + STUN)
ascii-chat client session-name \
  --webrtc-skip-host \
  --webrtc-skip-stun \
  --turn-servers turn:your-server.com:3478 \
  --prefer-webrtc
```

### Expected Outcome
✅ Connection succeeds via TURN relay
✅ Logs show "typ relay" candidates
✅ No "typ host" or "typ srflx" candidates

### Common Issues

**Timeout after 10s**
- TURN server unreachable
- Incorrect credentials
- Firewall blocking UDP/TCP to TURN server

**"No candidates gathered"**
- TURN server not configured
- Invalid TURN URL format

## Integration Tests

See `tests/integration/webrtc/test_turn_fallback.c` for automated tests.

**Note**: Tests are disabled by default (require TURN server setup).

To enable:
1. Set environment variables:
   ```bash
   export ASCII_CHAT_TURN_SERVERS="turn:your-server.com:3478"
   export ASCII_CHAT_TURN_USERNAME="your-username"
   export ASCII_CHAT_TURN_PASSWORD="your-password"
   ```
2. Remove `.disabled = true` from test definitions
3. Run: `ctest -R turn_fallback`

## Production Verification Checklist

- [ ] TURN servers configured in production deployment
- [ ] TURN credentials rotated regularly (security)
- [ ] Multiple TURN servers for redundancy
- [ ] TURN server health monitoring
- [ ] Logs show percentage of connections using relay
- [ ] Performance metrics for relay connections

## Issue #4 Status

🟡 **PARTIAL** - Test infrastructure created, manual testing required

**Remaining Work**:
- Set up TURN server for CI/CD
- Implement full integration tests
- Add performance benchmarks
- Document symmetric NAT test scenarios
