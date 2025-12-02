# Query Tool Troubleshooting

This guide covers common issues and solutions when using the query tool.

---

## Connection Issues

### "Connection refused" when curling

**Symptom**: `curl: (7) Failed to connect to localhost port 9999`

**Causes & Solutions**:

1. **Controller not running**
   ```bash
   # Check if controller is running
   pgrep -f ascii-query-server

   # Start it
   ./build/bin/ascii-query-server --attach $(pgrep ascii-chat) --port 9999
   ```

2. **Wrong port**
   ```bash
   # Check which port controller is using
   lsof -i -P | grep ascii-query
   ```

3. **Controller crashed** - Check for error output, may need to rebuild

---

## Attachment Issues

### "Failed to attach to process"

**macOS - Code signing required**:
```bash
# Check current signature
codesign -dvvv ./build/bin/ascii-query-server

# If unsigned or wrong entitlements, rebuild:
cmake --build build --target ascii-query-server
```

The build system should automatically sign with `get-task-allow` entitlement. If not:
```bash
# Manual signing (use your identity)
codesign -s "Your Developer ID" \
    --entitlements src/tooling/query/query.entitlements \
    ./build/bin/ascii-query-server
```

**Linux - ptrace restrictions**:
```bash
# Check current setting
cat /proc/sys/kernel/yama/ptrace_scope

# If 1 (restricted), either:
# Option 1: Run as root
sudo ./build/bin/ascii-query-server --attach <pid> --port 9999

# Option 2: Temporarily allow (requires root)
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

# Option 3: Use PR_SET_PTRACER in target (if you control it)
prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY);
```

**Docker**:
```bash
# Run container with ptrace capability
docker run --cap-add=SYS_PTRACE ...
```

### "Process is already being debugged"

Only one debugger can attach at a time. Check for:
```bash
# Other LLDB instances
pgrep lldb

# Other query controllers
pgrep ascii-query

# Kill if needed
pkill -f ascii-query-server
```

---

## Query Issues

### "Variable not found in current scope"

**Causes**:

1. **Variable optimized out** - Rebuild with `-O0`:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   ```

2. **Wrong file/line** - The line must be where variable is in scope:
   ```c
   void foo() {
       int x = 10;  // Query x here (line 2)
       // ...
   }  // x not in scope after this
   ```

3. **Typo in variable name** - Check spelling and case

4. **Static/local variable** - Must query at exact scope location

### "No debug info for file:line"

**Causes**:

1. **Release build** - Rebuild with debug symbols:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build
   ```

2. **Third-party library** - Some code may lack debug info

3. **Inline function** - May not have distinct line info

### "Timeout waiting for breakpoint"

**Causes**:

1. **Code path not executed** - The line may never be reached during your test

2. **Wrong file path** - Use full absolute path:
   ```bash
   curl 'localhost:9999/query?file=/full/path/to/src/server.c&line=100&name=x&break'
   ```

3. **Breakpoint in loop that exits** - Line was hit but exited before timeout

**Solutions**:
- Increase timeout: `&timeout=30000` (30 seconds)
- Verify the code path is being executed
- Use `/breakpoints` to check if breakpoint was set

---

## Performance Issues

### Queries are slow

**Normal causes**:
- Breakpoint mode is inherently slower (waits for execution)
- Large struct expansion generates many sub-queries
- First query after attach may be slower

**Solutions**:
- Use immediate mode for quick checks
- Limit expansion depth: `&depth=1`
- Keep controller running between queries

### Target runs slowly when controller attached

LLDB attachment has some overhead. This is expected but should be minimal.

If severely impacted:
- Detach when not actively querying
- Avoid setting many breakpoints
- Check if other debuggers are attached

---

## Build Issues

### "LLDB not found" during CMake

```bash
# macOS - Install Xcode command line tools
xcode-select --install

# Linux - Install LLDB development package
sudo apt install lldb liblldb-dev  # Ubuntu/Debian
sudo dnf install lldb-devel        # Fedora
```

### Query tool not built

Ensure you enable the option:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build
```

Check that LLDB was found:
```
-- Found LLDB library: /path/to/liblldb.dylib
-- Building query tool
```

---

## Platform-Specific Issues

### macOS: "Debugger was denied access"

System Integrity Protection may block debugging. Either:
1. Sign the binary with proper entitlements (preferred)
2. Disable SIP (not recommended for production machines)

### Linux: "Operation not permitted"

See ptrace restrictions above. Also check:
```bash
# SELinux may block
getenforce
# If Enforcing, try:
sudo setenforce 0
```

### Windows: "Access denied"

Run as Administrator, or ensure target was started by same user.

---

## Getting Help

### Enable debug logging

```bash
export LOG_LEVEL=0  # DEBUG level
./build/bin/ascii-query-server --attach <pid> --port 9999
```

### Check controller health

```bash
curl localhost:9999/
curl localhost:9999/process
```

### Collect diagnostic info

```bash
# Controller version/state
curl localhost:9999/

# Target process info
curl localhost:9999/process

# Thread state
curl localhost:9999/threads

# Stack frames (if stopped)
curl localhost:9999/frames
```

### Still stuck?

1. Check [QUERY_TOOL_PLAN.md](QUERY_TOOL_PLAN.md) for implementation details
2. Search existing issues on GitHub
3. File a new issue with:
   - Platform (macOS version, Linux distro, Windows version)
   - Error messages
   - Steps to reproduce
   - Debug log output
