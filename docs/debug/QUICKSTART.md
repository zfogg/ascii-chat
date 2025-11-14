# Debug Instrumentation - Quick Start Guide

**You have a crash. Here's how to find it in 5 minutes.**

---

## Step 1: Build with Instrumentation (2 minutes)

```bash
# Clean rebuild with instrumentation enabled
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCII_BUILD_WITH_INSTRUMENTATION=ON
cmake --build build
```

**What this does:** Rewrites your source code to log every statement before it executes.

**Build time:** 2-3x longer than normal (one-time cost).

---

## Step 2: Run with Logging Enabled (reproduce the crash)

```bash
# Enable instrumentation logging
export ASCII_INSTR_ENABLE=1

# (Optional) Reduce noise - only log suspect module
export ASCII_INSTR_INCLUDE=network  # Example: focus on network.c

# Run your program
./build/bin/ascii-chat server

# ... program runs and crashes ...
```

**What to expect:**
- Program runs 5-50x slower (this is normal)
- Log files appear in `/tmp/ascii-instr-*.log` (one per thread)
- Last line logged before crash = the bug location

---

## Step 3: Find the Crash Location (30 seconds)

```bash
# Show last executed line per thread
./build/bin/ascii-instr-report

# Example output:
# === Thread 12345 (ascii-chat-server) ===
# Last statement: lib/network.c:512 in socket_send()
# Snippet: send_packet(queue, payload);
# Timestamp: 2025-11-14T10:23:45.123456789Z
#
# === Thread 67890 (render-thread) ===
# Last statement: lib/mixer.c:128 in mix_audio()
# Snippet: memcpy(output, samples, size);
# Timestamp: 2025-11-14T10:23:45.123456500Z
```

**Interpreting results:**
- The thread with the latest timestamp is likely the crashing thread
- The last logged statement is where the crash occurred (or very close)
- Go to that file:line and inspect the code

---

## Step 4: Fix the Bug

**Common patterns:**

### Pattern 1: Crash in the logged statement
```c
// Last logged line: lib/network.c:512
send_packet(queue, payload);  // ← Crash here
```
**Fix:** Check if `queue` or `payload` is NULL.

### Pattern 2: Crash in called function
```c
// Last logged line: lib/network.c:512
send_packet(queue, payload);
  // ↓ Crash actually in send_packet() implementation
```
**Fix:** Narrow instrumentation to the called function:
```bash
export ASCII_INSTR_INCLUDE=send_packet
```

### Pattern 3: Multi-threaded crash
```c
// Thread A last line: client_list[i]->status = ACTIVE;
// Thread B last line: free(client_list[i]);
```
**Fix:** Race condition - add mutex protection.

---

## Common Mistakes

### Mistake 1: Forgetting `ASCII_INSTR_ENABLE=1`
**Symptom:** No log files created, program runs normally.

**Fix:**
```bash
export ASCII_INSTR_ENABLE=1  # Required!
./build/bin/ascii-chat server
```

### Mistake 2: Too much noise
**Symptom:** Millions of log lines, can't find the crash.

**Fix:** Use filters to narrow scope:
```bash
# Only log files with "network" in path
export ASCII_INSTR_INCLUDE=network

# Only log specific function
export ASCII_INSTR_ONLY=func=handle_client

# Log every 10th statement (reduce volume)
export ASCII_INSTR_RATE=10
```

### Mistake 3: Log files not found
**Symptom:** `ascii-instr-report` says "No log files found".

**Possible causes:**
1. `ASCII_INSTR_ENABLE` not set
2. Custom output directory not specified correctly
3. Program exited before crash (no logs written)

**Fix:**
```bash
# Check if logs exist
ls -lh /tmp/ascii-instr-*.log

# Or specify custom directory
export ASCII_INSTR_OUTPUT_DIR=/tmp/crash-logs
mkdir -p /tmp/crash-logs
```

### Mistake 4: Stale logs from previous run
**Symptom:** Logs don't update, always show old crash.

**Fix:**
```bash
# Clean old logs before each run
rm -f /tmp/ascii-instr-*.log
./build/bin/ascii-chat server
```

---

## Advanced Filtering Examples

### Focus on single file
```bash
export ASCII_INSTR_INCLUDE=network.c
```

### Focus on module
```bash
# All files under lib/network/ or src/network/
export ASCII_INSTR_ONLY=network:*
```

### Focus on specific functions
```bash
export ASCII_INSTR_ONLY=func=send_*,func=receive_*
```

### Combine filters
```bash
# Only network module, only send functions, every 5th statement
export ASCII_INSTR_ONLY=network:*
export ASCII_INSTR_INCLUDE=send
export ASCII_INSTR_RATE=5
```

### Focus on specific thread
```bash
# First, run without filter to see thread IDs
./build/bin/ascii-instr-report

# Then re-run with thread filter
export ASCII_INSTR_THREAD=12345
./build/bin/ascii-chat server
```

---

## When Instrumentation Doesn't Help

### Use AddressSanitizer (ASan) instead when:
- Crash is due to memory corruption (buffer overflow, use-after-free)
- You need to detect the error, not just locate it
- Instrumentation overhead is prohibitive (> 50x slowdown)

```bash
# Build with ASan instead
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCII_BUILD_WITH_INSTRUMENTATION=OFF
cmake --build build
./build/bin/ascii-chat server
```

### Use rr (record-replay) when:
- Crash is non-deterministic (hard to reproduce)
- You need to inspect program state at arbitrary points
- Linux is your platform

```bash
# Record execution
rr record ./build/bin/ascii-chat server

# Replay with debugger
rr replay
```

### Use manual printf() when:
- Bug is localized to 1-2 functions
- You need to inspect variable values (not just locations)
- Instrumentation logs are too noisy

---

## Workflow Checklist

- [ ] Build with `-DASCII_BUILD_WITH_INSTRUMENTATION=ON`
- [ ] Set `ASCII_INSTR_ENABLE=1`
- [ ] (Optional) Set filters to reduce noise
- [ ] Run program and reproduce crash
- [ ] Run `ascii-instr-report` to find last logged lines
- [ ] Inspect code at crash location
- [ ] Fix bug
- [ ] Rebuild without instrumentation for normal use

---

## Getting Help

**Read the full docs:**
- `docs/debug-instrumentation.md` - Complete reference
- `docs/debug/PLAN.md` - Design rationale
- `docs/debug/REVIEW.md` - Technical review

**Check environment variables:**
```bash
env | grep ASCII_INSTR
```

**Enable debug output:**
```bash
export ASCII_INSTR_ECHO_STDERR=1  # Echo logs to stderr
```

**Common issues:**
- macOS Criterion tests fail → Use Docker: `docker-compose -f tests/docker-compose.yml run --rm ascii-chat-tests`
- Windows regex not working → Use glob patterns instead (`ASCII_INSTR_ONLY=file=lib/*.c`)

---

**Last Updated:** 2025-11-14
**For Issues:** Report at https://github.com/zfogg/ascii-chat/issues
