# Query Tool (ascii-query-server)

Runtime variable inspection tool for ascii-chat debug builds.

## Overview

The query tool enables HTTP-based variable inspection using LLDB:

```
┌─────────────────────────┐           ┌─────────────────────────┐
│  ascii-chat (TARGET)    │           │  ascii-query-server     │
│                         │           │  (CONTROLLER)           │
│  Your application       │◄──LLDB───►│  HTTP server :9999      │
│  runs normally          │           │  Accepts curl requests  │
└─────────────────────────┘           └─────────────────────────┘
```

## Quick Start

```bash
# Build with query support
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build

# Start your application
./build/bin/ascii-chat server &

# Attach query controller
./build/bin/ascii-query-server --attach $(pgrep ascii-chat) --port 9999

# Query variables
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'

# Interactive breakpoint mode
curl 'localhost:9999/query?file=src/server.c&line=100&name=x&break'
curl 'localhost:9999/query?name=other_var'  # While stopped
curl -X POST 'localhost:9999/continue'
```

## Files

- `main.cpp` - Controller entry point
- `lldb_controller.h/cpp` - LLDB SB API wrapper
- `http_server.h/cpp` - HTTP server implementation
- `variable_reader.h/cpp` - Variable reading via LLDB
- `breakpoint_manager.h/cpp` - Breakpoint lifecycle management

## See Also

- `lib/tooling/query/query.h` - Public C API for auto-spawn
- `docs/tooling/query-tool.md` - User documentation
- `docs/tooling/QUERY_TOOL_PLAN.md` - Implementation plan
