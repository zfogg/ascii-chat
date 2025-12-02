# Query Tool User Guide

The query tool enables runtime variable inspection in debug builds of ascii-chat. It uses an external LLDB process to read variable values via HTTP queries, allowing you to inspect program state without modifying code or using a full debugger.

## Quick Start

### 1. Build with Query Support

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build
```

### 2. Launch Mode A: Standalone Controller

```bash
# Terminal 1: Start your application
./build/bin/ascii-chat server --port 8080

# Terminal 2: Attach the query controller
./build/bin/ascii-query-server --attach $(pgrep ascii-chat) --port 9999

# Terminal 3: Query variables
curl 'localhost:9999/query?file=src/server/main.c&line=100&name=options'
```

### 3. Launch Mode B: Auto-Spawn (Recommended)

Add to your application code:

```c
#include "tooling/query/query.h"

int main(void) {
    // Auto-spawn controller attached to this process
    int port = QUERY_INIT(9999);
    if (port > 0) {
        printf("Query server at http://localhost:%d\n", port);
    }

    // ... your application runs ...

    QUERY_SHUTDOWN();
    return 0;
}
```

## Key Concepts

### Architecture

```
┌─────────────────────────┐         ┌─────────────────────────┐
│  Your Application       │         │  ascii-query-server     │
│  (target)               │◄──LLDB─►│  (controller)           │
│                         │         │                         │
│  State: RUNNING         │         │  HTTP server on :9999   │
│         or STOPPED      │         │  Always responsive!     │
└─────────────────────────┘         └─────────────────────────┘
                                              ▲
                                              │ curl
                                              │
                                    ┌─────────────────────────┐
                                    │  curl / browser / vim   │
                                    └─────────────────────────┘
```

**Key insight**: When your application is stopped at a breakpoint, the controller process is still running and serving HTTP requests. This allows you to query variables and control execution.

### Query Modes

1. **Immediate Mode**: Briefly pauses target, reads variable, resumes. Fast and non-interactive.

2. **Breakpoint Mode** (`&break`): Sets breakpoint, waits for hit, keeps target stopped for interactive inspection.

## Common Workflows

### Inspecting a Variable at a Specific Line

```bash
# Query 'client_count' when execution reaches server.c:100
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'
```

Response:
```json
{
  "status": "ok",
  "result": {
    "name": "client_count",
    "value": "5",
    "type": "int"
  }
}
```

### Interactive Debugging Session

```bash
# Step 1: Set breakpoint and stop
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count&break'
# Response: {"stopped": true, "result": {"value": "5"}}

# Step 2: Query more variables (no file:line needed while stopped)
curl 'localhost:9999/query?name=client.socket.fd'
curl 'localhost:9999/query?name=options&expand'

# Step 3: Check stack frames
curl 'localhost:9999/frames'

# Step 4: Resume execution
curl -X POST 'localhost:9999/continue'
```

### Expanding Struct Members

```bash
# Expand struct to see all members
curl 'localhost:9999/query?file=src/server.c&line=100&name=client&expand'
```

Response:
```json
{
  "result": {
    "name": "client",
    "type": "client_t",
    "children": [
      {"name": "id", "type": "uint32_t", "value": "42"},
      {"name": "socket", "type": "socket_info_t", "value": "{...}"},
      {"name": "socket.fd", "type": "int", "value": "7"}
    ]
  }
}
```

### Querying Array Elements

```bash
curl 'localhost:9999/query?file=src/server.c&line=100&name=clients[0]'
curl 'localhost:9999/query?file=src/server.c&line=100&name=buffer[0..10]'
```

### Querying Parent Stack Frames

```bash
# Query variable from caller's frame
curl 'localhost:9999/query?name=argc&frame=1'

# Query by function name
curl 'localhost:9999/query?name=main_opts&frame=main'
```

## Editor Integration

### Vim/Neovim

Add to your vimrc:

```vim
" Query variable under cursor
function! QueryVariable()
    let var = expand('<cword>')
    let file = expand('%:p')
    let line = line('.')
    let cmd = 'curl -s "localhost:9999/query?file=' . file . '&line=' . line . '&name=' . var . '"'
    echo system(cmd)
endfunction

nnoremap <leader>q :call QueryVariable()<CR>
```

### VS Code

Use the REST Client extension to send queries, or create a task:

```json
{
  "label": "Query Variable",
  "type": "shell",
  "command": "curl 'localhost:9999/query?file=${file}&line=${lineNumber}&name=${selectedText}'"
}
```

## Platform Notes

### macOS

The query controller requires code signing with the `get-task-allow` entitlement:

```bash
# Check if binary is signed
codesign -dvvv ./build/bin/ascii-query-server

# Sign with entitlements (done automatically by CMake)
codesign -s "Your Identity" --entitlements query.entitlements ./build/bin/ascii-query-server
```

### Linux

Check ptrace permissions:

```bash
cat /proc/sys/kernel/yama/ptrace_scope
# 0 = permissive (debugging works)
# 1 = restricted (may need sudo)

# Temporarily allow (requires root)
echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope
```

For Docker:
```bash
docker run --cap-add=SYS_PTRACE ...
```

### Windows

Uses CreateProcess for spawning. Ensure the controller binary is in PATH or provide full path.

## Debug Tips

### Enable Verbose Logging

```bash
# Set log level to debug
export LOG_LEVEL=0
./build/bin/ascii-query-server --attach <pid> --port 9999
```

### Check Controller Status

```bash
# Health check
curl localhost:9999/

# Process info
curl localhost:9999/process

# List threads
curl localhost:9999/threads
```

### Common Issues

See [query-troubleshooting.md](query-troubleshooting.md) for solutions to common problems.

## Limitations

- **Debug builds only**: Query tool compiles out completely in release builds
- **One controller per target**: Cannot attach multiple controllers to same process
- **LLDB required**: Requires LLDB development libraries to build
- **Local only**: HTTP server binds to localhost for security

## See Also

- [query-api.md](query-api.md) - Full HTTP API reference
- [query-troubleshooting.md](query-troubleshooting.md) - Common issues and solutions
- [QUERY_TOOL_PLAN.md](QUERY_TOOL_PLAN.md) - Implementation details
