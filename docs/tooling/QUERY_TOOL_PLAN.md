# Query Tool Plan

**Branch:** `query-tool`
**Locations:** `src/tooling/query/`, `lib/tooling/query/`

---

## Action Plan: From Zero to Merged

### Phase 1: Foundation (Commits 1-3)

> **STATUS: COMPLETE** - All items implemented in initial commit `6e4b546`

```
[x] 1.1  Create branch: git checkout -b query-tool
[x] 1.2  Create directory structure:
         - lib/tooling/query/
         - src/tooling/query/
         - cmake/tooling/Query.cmake
         - tests/unit/tooling/query_*.c
[x] 1.3  Write stub query.h with API declarations
[x] 1.4  Write CMakeLists.txt for lib and src
[x] 1.5  Write Query.cmake with ascii_query_prepare()/finalize()
[x] 1.6  Verify build: cmake -B build -DASCIICHAT_BUILD_WITH_QUERY=ON
[x] 1.7  COMMIT 1: "feat(query): add project scaffolding"

[x] 2.1  Implement LLDBController class (attach/detach/stop/resume)
[x] 2.2  Add CLI argument parsing (--attach, --port, --help)
[x] 2.3  Test: attach to running ascii-chat process
[x] 2.4  COMMIT 2: "feat(query): implement LLDB attachment"

[x] 3.1  Implement HTTP server (single-threaded, like lldb_inspect)
[x] 3.2  Add routes: GET /, GET /process, GET /threads
[x] 3.3  Add JSON serialization helpers
[x] 3.4  Test: curl localhost:9999/process returns JSON
[x] 3.5  COMMIT 3: "feat(query): add HTTP server skeleton"
```

**Notes:**
- All Phase 1 items were combined into a single initial commit (`6e4b546`)
- LLDBController in `src/tooling/query/lldb_controller.cpp` implements full LLDB SB API integration
- HTTP server uses single-threaded design with routes for /, /process, /threads, /query, /breakpoints, /continue, /stop, /step

### Phase 2: Core Features (Commits 4-7)

> **STATUS: COMPLETE** - All Phase 2 items implemented (4.x-7.x)

```
[x] 4.1  Implement VariableReader class
[x] 4.2  Add file:line:name lookup via LLDB SBTarget
[x] 4.3  Support primitive types (int, float, char*, pointers)
[x] 4.4  Add GET /query endpoint
[x] 4.5  Test: query a simple variable in ascii-chat
[x] 4.6  COMMIT 4: "feat(query): implement variable reading"

[x] 5.1  Implement BreakpointManager class
[x] 5.2  Add breakpoint set/remove/list operations
[x] 5.3  Implement wait-for-hit with timeout
[x] 5.4  Add mode=breakpoint parameter to /query (uses &break param)
[x] 5.5  Add GET/POST/DELETE /breakpoints endpoints
[x] 5.6  Test: breakpoint query waits and captures value
[x] 5.7  COMMIT 5: "feat(query): add breakpoint mode"

[x] 6.1  Implement query.c with fork/exec spawn (Unix)
[x] 6.2  Implement query.c with CreateProcess spawn (Windows)
[x] 6.3  Add wait_for_http_ready() health check
[x] 6.4  Add QUERY_INIT/QUERY_SHUTDOWN macros
[x] 6.5  Test: ascii-chat auto-spawns controller on startup
[ ] 6.6  COMMIT 6: "feat(query): add auto-spawn integration"

[x] 7.1  Implement recursive struct member traversal
[x] 7.2  Add expand=true and depth=N parameters
[x] 7.3  Handle arrays (show first N elements, limited to 100)
[x] 7.4  Handle char[] as strings (via LLDB GetSummary)
[x] 7.5  Add circular reference detection (depth limit prevents infinite recursion)
[x] 7.6  Test: expand complex nested struct
[ ] 7.7  COMMIT 7: "feat(query): add struct expansion"
```

**Notes (4.x Variable Reading):**
- Variable reading integrated into LLDBController class (not separate VariableReader)
- `queryVariable()` method in `lldb_controller.cpp:346-462` handles file:line:name lookup
- Supports primitives, pointers, and struct member access via dot notation
- GET /query endpoint fully functional

**Notes (5.x Breakpoint Mode):**
- Breakpoint functionality integrated into LLDBController (not separate BreakpointManager class)
- Methods: `setBreakpoint()`, `removeBreakpoint()`, `getBreakpoints()`, `waitForBreakpoint()`
- HTTP endpoints: GET/POST/DELETE /breakpoints, GET /query with &break parameter
- Test script: `tests/scripts/test_query_breakpoints.sh` (commit `2af4403`)
- Test fixture: `tests/fixtures/query_test_target.c`
- **Important:** Breakpoints stop BEFORE line executes - query variables initialized on previous lines

**Notes (6.x Auto-Spawn):**
- `lib/tooling/query/query.c` fully implemented with:
  - Unix: fork/exec to spawn controller process
  - Windows: CreateProcess for spawning
  - `wait_for_http_ready()` health check with timeout
  - `find_query_server_path()` searches common locations + `ASCIICHAT_QUERY_SERVER` env var
- Test script: `tests/scripts/test_query_autospawn.sh`
- Test fixture: `tests/fixtures/query_autospawn_test.c`

**Notes (7.x Struct Expansion):**
- Already implemented in initial commit via `valueToInfo()` recursive expansion
- HTTP parameters: `&expand` (default depth 3) or `&depth=N` (explicit)
- Arrays limited to 100 elements to prevent huge responses
- Char arrays show string summary via LLDB's `GetSummary()`
- Depth limit prevents infinite recursion with circular references
- Test script: `tests/scripts/test_query_struct_expansion.sh`

### Phase 3: Build Integration (Commit 8)

> **STATUS: COMPLETE** - CMake integration working, tested on macOS and Linux (Docker)

```
[x] 8.1  Finalize Query.cmake with proper LLDB discovery
[x] 8.2  Add ASCIICHAT_BUILD_WITH_QUERY option to a cmake/ module.
[x] 8.3  Ensure query tool only builds in Debug mode
[x] 8.4  Link ascii-query-runtime to ascii-chat target when ASCIICHAT_BUILD_WITH_QUERY
[x] 8.5  Test: full build from clean with query enabled
[x] 8.6  COMMIT 8: "feat(query): complete CMake integration"
```

**Notes:**
- `cmake/tooling/Query.cmake` implements `ascii_query_prepare()`/`ascii_query_finalize()`
- LLDB discovered via `find_library(LLDB_LIBRARY NAMES lldb liblldb)`
- Query tool built as ExternalProject to avoid inheriting musl/static linking flags
- Built binary goes to `.deps-cache/query-tool/ascii-query-server`
- Runtime library (`ascii-query-runtime`) builds as STATIC library, linked to ascii-chat
- Include path fixed to allow `#include "tooling/query/query.h"`
- Requires code signing on macOS (entitlements in `src/tooling/query/query.entitlements`)
- Tested in Docker (Linux x86_64) - runtime library links correctly

### Phase 4: Platform Testing (Commits 9-11)

```
[x] 9.1  Test on macOS ARM64 (Apple Silicon)
[ ] 9.2  Test on macOS x86_64 (Intel, if available) - User will test later
[x] 9.3  Handle code signing (get-task-allow entitlement)
[-] 9.4  Write tests/scripts/test_query_macos.sh (SKIP: use existing test scripts)
[x] 9.5  Fix any macOS-specific issues (none found)
[ ] 9.6  COMMIT 9: "test(query): verify macOS support"

[ ] 10.1 Test on Linux x86_64 (Ubuntu 22.04)
[ ] 10.2 Handle ptrace_scope restrictions
[ ] 10.3 Test in Docker with --cap-add=SYS_PTRACE
[ ] 10.4 Write tests/scripts/test_query_linux.sh
[ ] 10.5 Fix any Linux-specific issues
[ ] 10.6 COMMIT 10: "test(query): verify Linux support"

[ ] 11.1 Test on Windows x86_64
[ ] 11.2 Verify CreateProcess spawn works
[ ] 11.3 Verify Winsock HTTP server works
[ ] 11.4 Handle Windows path separators
[ ] 11.5 Write tests/scripts/test_query_windows.ps1
[ ] 11.6 Fix any Windows-specific issues
[ ] 11.7 COMMIT 11: "test(query): verify Windows support"
```

### Phase 5: Testing & Quality (Commits 12-13)

> **STATUS: COMPLETE** - Unit and integration tests created (commit `d5a8eef`)

```
[x] 12.1 Write tests/unit/tooling/query_api_test.c (updated query_test.c with comprehensive tests)
[-] 12.2 Write tests/unit/tooling/query_http_test.cpp (covered by integration tests)
[-] 12.3 Write tests/unit/tooling/query_variable_test.cpp (covered by integration tests)
[x] 12.4 Create tests/fixtures/query_test_target.c
[ ] 12.5 Verify all unit tests pass on all platforms
[x] 12.6 COMMIT 12: "test(query): add unit tests"

[x] 13.1 Write tests/integration/tooling/query_integration_test.sh
[x] 13.2 Write tests/integration/tooling/query_stress_test.sh
[x] 13.3 Test: attach → query → detach → server continues
[x] 13.4 Test: multiple concurrent queries
[x] 13.5 Test: controller crash recovery
[x] 13.6 COMMIT 13: "test(query): add integration tests"
```

**Notes:**
- Test fixture `tests/fixtures/query_test_target.c` created with:
  - Global variable `g_global_counter` for testing global queries
  - `TestData` struct with nested `point` struct for member access testing
  - `process_data()` function with local variables for breakpoint testing
  - Designed to run in infinite loop for debugging attachment
- Breakpoint test script `tests/scripts/test_query_breakpoints.sh` created (commit `2af4403`)
  - Tests: GET/POST/DELETE /breakpoints, &break parameter, conditional breakpoints
  - Validates breakpoint set/list/remove lifecycle
  - Tests process control: /continue, /stop, /step

**Phase 5 Tests Added (commit `d5a8eef`):**
- `tests/unit/tooling/query_test.c` - Comprehensive API tests:
  - QUERY_INIT/SHUTDOWN lifecycle tests
  - State consistency tests
  - Release build macro verification
- `tests/integration/tooling/query_integration_test.sh` - Full HTTP endpoint tests:
  - All HTTP endpoints (/, /process, /threads, /frames, /breakpoints)
  - Process control (/stop, /continue, /step)
  - Variable queries with breakpoints
  - Struct expansion
  - Detach/survive behavior
- `tests/integration/tooling/query_stress_test.sh` - Load testing:
  - Rapid sequential requests
  - Concurrent request handling
  - Stop/continue cycling
  - Invalid request resilience

### Phase 6: Documentation & CI (Commits 14-15)

```
[ ] 14.1 Write docs/tooling/query-tool.md (user guide)
[ ] 14.2 Write docs/tooling/query-api.md (API reference)
[ ] 14.3 Write docs/tooling/query-troubleshooting.md
[ ] 14.4 Add Doxygen comments to query.h
[ ] 14.5 Write src/tooling/query/README.md (quick reference)
[ ] 14.6 COMMIT 14: "docs(query): add documentation"

[ ] 15.1 Create .github/workflows/query-tool.yml
[ ] 15.2 Add macOS CI job (macos-latest)
[ ] 15.3 Add Linux CI job (ubuntu container with SYS_PTRACE)
[ ] 15.4 Add Windows CI job (windows-latest)
[ ] 15.5 Verify all CI jobs pass
[ ] 15.6 COMMIT 15: "ci(query): add CI workflow"
```

### Phase 7: Merge Preparation (Commit 16)

```
[ ] 16.1 Run full test suite on macOS
[ ] 16.2 Run full test suite on Linux
[ ] 16.3 Run full test suite on Windows
[ ] 16.4 Run ASan/memory leak checks
[ ] 16.5 Run code formatter: cmake --build build --target format
[ ] 16.6 Fix any compiler warnings
[ ] 16.7 Update CLAUDE.md with query tool section
[ ] 16.8 Update README.md with query tool mention
[ ] 16.9 Squash/rebase for clean history (if needed)
[ ] 16.10 COMMIT 16: "chore(query): final polish"

[ ] 17.1 Create PR: query-tool → master
[ ] 17.2 Fill PR description with feature summary
[ ] 17.3 Request review
[ ] 17.4 Address review feedback
[ ] 17.5 Merge to master
```

### Quick Reference: Key Commands

```bash
# Build with query tool
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build

# Test standalone controller
./build/bin/ascii-chat server &
./build/bin/ascii-query-server --attach $(pgrep ascii-chat) --port 9999
curl localhost:9999/query?file=src/server.c&line=50&name=options

# Test auto-spawn (in ascii-chat code)
QUERY_INIT(9999);
// ... app runs ...
QUERY_SHUTDOWN();

# Run tests (does criterion work to test this? write custom tests if not)
cd build && ctest -R test_query
```

### Milestones

| Milestone            | Commits | Criteria                                             | Status |
| -------------------- | ------- | ---------------------------------------------------- | ------ |
| **MVP**              | 1-4     | Can query primitive variables via HTTP               | ✅ DONE |
| **Feature Complete** | 1-8     | All query modes, struct expansion, CMake integration | ✅ DONE |
| **Platform Ready**   | 1-11    | Works on macOS, Linux, Windows                       | ⏳ Pending |
| **Production Ready** | 1-16    | Tests, docs, CI, ready for merge                     | ⏳ Pending |

**Current Status (2025-12-01):**
- MVP achieved: Variable queries work via HTTP on macOS
- Breakpoint mode fully functional
- Struct expansion (7.x) fully implemented and tested
- Auto-spawn (6.x) fully implemented and tested
- Phase 3 (Build Integration) complete - runtime library links correctly
- Feature Complete milestone achieved, ready for platform testing (Phase 4)

---

## Overview

The query tool is a debug utility that enables runtime variable inspection via HTTP queries. It uses an **external LLDB process** attached to the running application, providing robust breakpoint support and full struct member traversal without any self-debugging complexity.

### Key Features

- Query variables by `file:line:variable` (including struct members like `client.socket.fd`)
- HTTP API for integration with external tools (curl, editors, scripts)
- External LLDB process handles all debugging complexity
- Full breakpoint support with proper thread handling
- Compiles out completely in release builds (`#ifndef NDEBUG`)

### Why External LLDB?

- **Robust**: LLDB handles all edge cases (thread safety, memory protection, signals)
- **Cross-platform**: Works on macOS, Linux, Windows without platform-specific code
- **Full-featured**: Native struct traversal, watchpoints, conditional breakpoints
- **No self-patching**: No need to modify running code or handle SIGTRAP ourselves

### Why HTTP Still Works When Target Is Stopped

**Critical Architecture Point:** The HTTP server runs in the **controller process**, NOT the target process.

```
When target hits a breakpoint:
┌─────────────────────────┐           ┌─────────────────────────┐
│  ascii-chat (TARGET)    │           │  ascii-query-server     │
│                         │           │  (CONTROLLER)           │
│  State: STOPPED         │           │  State: RUNNING ✓       │
│  Can't do anything      │◄──LLDB───►│  HTTP server active ✓   │
│  Waiting at breakpoint  │           │  Accepts curl requests  │
│                         │           │  Sends LLDB commands    │
└─────────────────────────┘           └─────────────────────────┘
         ▲                                       ▲
         │                                       │
    Your code is                           Your curl goes
    paused here                            HERE (works!)
```

This is the key insight: when you `curl localhost:9999/continue`, the **controller** receives that request (it's still running), then it calls `SBProcess::Continue()` to resume the target.

If we had used self-debugging (int3 in-process), we'd have exactly the problem you identified - the stopped process couldn't serve HTTP. The external LLDB architecture avoids this entirely.

---

## Architecture

### High-Level Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│  Target Process (ascii-chat server/client)                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Application runs normally                                          │
│  ├─ No instrumentation needed                                       │
│  ├─ Just needs debug symbols (-g)                                   │
│  └─ Optional: QUERY_INIT() to auto-spawn controller                 │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
              ▲
              │ ptrace/task_for_pid (LLDB attachment)
              │
┌─────────────────────────────────────────────────────────────────────┐
│  Query Controller Process (ascii-query-server)                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  LLDB Driver:                                                       │
│  ├─ Attaches to target process via LLDB SB API                      │
│  ├─ Sets/removes breakpoints on demand                              │
│  ├─ Reads variable values via SBFrame::FindVariable()               │
│  └─ Traverses struct members via SBValue API                        │
│                                                                     │
│  HTTP Server:                                                       │
│  ├─ Listens on localhost:PORT                                       │
│  ├─ Translates HTTP requests → LLDB commands                        │
│  └─ Returns JSON responses                                          │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
              ▲
              │ HTTP (localhost:PORT)
              │
┌─────────────────────────────────────────────────────────────────────┐
│  External Tools (curl, vim, vscode, browser)                        │
└─────────────────────────────────────────────────────────────────────┘
```

### Process Model

```
┌──────────────┐         ┌──────────────────────┐
│ ascii-chat   │ ◄─────► │ ascii-query-server   │
│ (target)     │  LLDB   │ (controller)         │
│              │ attach  │                      │
│ PID: 12345   │         │ HTTP :9999           │
└──────────────┘         └──────────────────────┘
                                   ▲
                                   │ curl/browser
                                   │
                         ┌─────────────────────┐
                         │ Developer           │
                         │ curl localhost:9999 │
                         └─────────────────────┘
```

### Two Launch Modes

#### Mode A: Standalone Controller

```bash
# Terminal 1: Start ascii-chat normally
./build/bin/ascii-chat server --port 8080

# Terminal 2: Attach query controller
./build/bin/ascii-query-server --attach $(pgrep ascii-chat) --port 9999

# Terminal 3: Query variables
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'
```

#### Mode B: Auto-Spawn from Target (Debug Builds)

```c
// In ascii-chat, debug builds auto-spawn the controller
QUERY_INIT(9999);  // Spawns ascii-query-server attached to self
// ... application runs ...
QUERY_SHUTDOWN();  // Terminates controller
```

---

## Query Syntax

### Basic Variable Query (while running)

```
GET /query?file=lib/network.c&line=142&name=client_count
```

Briefly stops target, reads variable, resumes. Fast, non-interactive.

### Struct Member Query

```
GET /query?file=src/server.c&line=256&name=client.socket.fd
GET /query?file=src/server.c&line=256&name=client->buffer[0]
GET /query?file=lib/mixer.c&line=89&name=state.channels[0].volume
```

### Interactive Breakpoint Mode (NEW - `&break` parameter)

**Step 1: Set breakpoint and stop**

```bash
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count&break'
```

- Sets breakpoint at `server.c:100`
- Blocks until that line executes
- Returns value of `client_count` AND keeps target stopped
- Response includes `"stopped": true`

**Step 2: Query more variables while stopped**

```bash
# No file:line needed - queries current stopped frame
curl 'localhost:9999/query?name=frame.width'
curl 'localhost:9999/query?name=options&expand=true'
curl 'localhost:9999/query?name=client->socket.fd'

# Query parent stack frames
curl 'localhost:9999/query?name=argc&frame=1'        # caller's frame
curl 'localhost:9999/query?name=main_opts&frame=main' # by function name

# List all variables in current scope
curl 'localhost:9999/query?list=scope'

# List all threads
curl 'localhost:9999/threads'
```

All queries see **exact same program state** - no race conditions.

**Step 3: Resume execution**

```bash
curl -X POST 'localhost:9999/continue'
```

### Interactive Mode Parameters

| Parameter     | Description                                                   |
| ------------- | ------------------------------------------------------------- |
| `&break`      | Stop at breakpoint and stay stopped for interactive queries   |
| `&timeout=N`  | Timeout in ms waiting for breakpoint (default: 5000)          |
| `&frame=N`    | Query variable from stack frame N (0=current, 1=caller, etc.) |
| `&frame=func` | Query variable from named function's frame                    |
| `&thread=N`   | Query/switch to thread N                                      |

### Interactive Mode Response

```json
{
  "status": "ok",
  "stopped": true,
  "breakpoint": {
    "file": "src/server.c",
    "line": 100,
    "hit_count": 1
  },
  "thread": {
    "id": 1,
    "name": "main",
    "frame": "process_clients"
  },
  "result": {
    "name": "client_count",
    "value": "5",
    "type": "int"
  }
}
```

### Query While Stopped Response

```json
{
  "status": "ok",
  "stopped": true,
  "result": {
    "name": "frame.width",
    "value": "1920",
    "type": "int"
  }
}
```

### Example Interactive Session

```bash
# Developer suspects bug in client handling at line 100
$ curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count&break'
{"stopped":true, "result":{"name":"client_count","value":"5"}}

# Hmm, 5 clients. Let me check the client array...
$ curl 'localhost:9999/query?name=clients[0]&expand=true'
{"result":{"name":"clients[0]","type":"client_t","members":[...]}}

# Check the socket state
$ curl 'localhost:9999/query?name=clients[0].socket.fd'
{"result":{"value":"7"}}

# What about the global stats?
$ curl 'localhost:9999/query?name=g_stats.bytes_sent'
{"result":{"value":"1048576"}}

# Let me see what the caller passed in
$ curl 'localhost:9999/query?name=max_clients&frame=1'
{"result":{"value":"10"}}

# Done investigating, continue
$ curl -X POST 'localhost:9999/continue'
{"status":"running"}
```

### Query Response

```json
{
  "status": "ok",
  "query": {
    "file": "lib/network.c",
    "line": 142,
    "name": "client.socket.fd"
  },
  "result": {
    "value": "7",
    "type": "int",
    "size": 4,
    "address": "0x7fff5fbff8ac"
  },
  "mode": "immediate",
  "target_pid": 12345,
  "timestamp_ns": 1234567890123456789
}
```

### Struct Expansion Response

```json
{
  "status": "ok",
  "query": {
    "file": "src/server.c",
    "line": 100,
    "name": "client",
    "expand": true
  },
  "result": {
    "value": "{...}",
    "type": "client_t",
    "size": 128,
    "address": "0x7fff5fbff800",
    "members": [
      { "name": "id", "type": "uint32_t", "value": "42", "offset": 0 },
      { "name": "socket", "type": "socket_info_t", "value": "{...}", "offset": 8 },
      { "name": "socket.fd", "type": "int", "value": "7", "offset": 8 },
      { "name": "socket.address", "type": "char[64]", "value": "\"127.0.0.1\"", "offset": 12 }
    ]
  }
}
```

---

## HTTP API Endpoints

### `GET /` - Status Page

Returns HTML status page showing:

- Attached process info (PID, name, state)
- Whether target is stopped or running
- Current breakpoint (if stopped)
- Interactive query form

### `GET /query` - Query Variable

**When target is RUNNING:**
| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `file` | Yes | - | Source file path (relative to project root) |
| `line` | Yes | - | Line number |
| `name` | Yes | - | Variable name (supports dot/arrow notation) |
| `break` | No | - | If present, stop at breakpoint and stay stopped |
| `timeout` | No | `5000` | Timeout in ms waiting for breakpoint |
| `expand` | No | `false` | Expand struct members recursively |
| `depth` | No | `3` | Max recursion depth for expansion |

**When target is STOPPED (after `&break`):**
| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `name` | Yes | - | Variable name (no file:line needed!) |
| `frame` | No | `0` | Stack frame index or function name |
| `thread` | No | current | Thread ID to query |
| `expand` | No | `false` | Expand struct members |
| `list` | No | - | Set to `scope` to list all variables in scope |

### `GET /threads` - List Threads

Returns all threads with:

- Thread ID and name
- Current function and line
- Thread state (running, stopped, etc.)

### `GET /frames` - List Stack Frames (when stopped)

Returns call stack for current thread:

- Frame index
- Function name
- File and line
- Arguments

### `GET /process` - Process Info

Returns:

- PID and process name
- State: `running`, `stopped`, `exited`
- If stopped: breakpoint info, current thread/frame
- Memory usage, loaded modules

### `POST /continue` - Continue Execution

Resumes target if stopped at breakpoint.

```json
{ "status": "running" }
```

### `POST /step` - Single Step (when stopped)

Execute one source line.

- `POST /step` - Step into
- `POST /step?over=true` - Step over (don't enter functions)
- `POST /step?out=true` - Step out (run until current function returns)

### `POST /detach` - Detach from Target

Cleanly detaches LLDB, target continues running independently.

### `GET /breakpoints` - List All Breakpoints

Returns all set breakpoints (persistent ones, not just interactive).

### `POST /breakpoints` - Set Persistent Breakpoint

For breakpoints that should persist across multiple hits:

```json
{
  "file": "src/server.c",
  "line": 100,
  "condition": "client_count > 5",
  "stop": true
}
```

### `DELETE /breakpoints/:id` - Remove Breakpoint

### Error Responses

```json
{
  "status": "error",
  "error": "not_stopped",
  "message": "Cannot query without file:line when target is running"
}
```

```json
{
  "status": "error",
  "error": "timeout",
  "message": "Breakpoint at server.c:100 not hit within 5000ms"
}
```

```json
{
  "status": "error",
  "error": "variable_not_found",
  "message": "Variable 'foo' not found in current scope"
}
```

---

## Implementation Plan

### Commit 1: Project Scaffolding

**Files:**

```
lib/tooling/query/
├── query.h                 # Public C API header
└── CMakeLists.txt          # Library build config

src/tooling/query/
├── main.cpp                # Controller entry point
├── CMakeLists.txt          # Executable build config
└── README.md               # Usage documentation

cmake/tooling/
└── Query.cmake             # CMake integration module

tests/unit/tooling/
└── query_test.c            # Test placeholder
```

**Deliverables:**

- [ ] Directory structure created
- [ ] Stub `query.h` with API declarations
- [ ] CMakeLists.txt that finds liblldb
- [ ] Empty `main.cpp` that links against LLDB
- [ ] `Query.cmake` with `ascii_query_prepare()` / `ascii_query_finalize()`

---

### Commit 2: LLDB Attachment & Process Control

**Files:**

```
src/tooling/query/
├── main.cpp                # CLI argument parsing
├── lldb_controller.h       # LLDB wrapper interface
├── lldb_controller.cpp     # LLDB SB API integration
└── process_info.h/cpp      # Process discovery utilities
```

**Implementation:**

```cpp
// lldb_controller.h
class LLDBController {
public:
    bool attach(pid_t pid);
    bool attachByName(const std::string& process_name);
    void detach();

    bool isAttached() const;
    pid_t targetPid() const;

    // Process control
    void stop();
    void resume();
    ProcessState state() const;

private:
    lldb::SBDebugger debugger_;
    lldb::SBTarget target_;
    lldb::SBProcess process_;
};
```

**Deliverables:**

- [ ] Attach to process by PID
- [ ] Attach to process by name
- [ ] Stop/resume target
- [ ] Clean detach
- [ ] Basic CLI: `ascii-query-server --attach <pid>`

**Test:**

```bash
# Start target
./build/bin/ascii-chat server &
PID=$!

# Attach controller
./build/bin/ascii-query-server --attach $PID --dry-run
# Should print: "Attached to process 12345 (ascii-chat)"
```

---

### Commit 3: HTTP Server Skeleton

**Files:**

```
src/tooling/query/
├── http_server.h           # HTTP server interface
├── http_server.cpp         # Minimal HTTP/1.1 implementation
└── json.h                  # Simple JSON serialization helpers
```

**Implementation:**

- Single-threaded HTTP server (like lldb_inspect)
- Routes: `/`, `/process`, `/threads`
- JSON responses with proper CORS headers

**Deliverables:**

- [ ] HTTP server listening on configurable port
- [ ] `GET /` returns status HTML
- [ ] `GET /process` returns target info JSON
- [ ] `GET /threads` returns thread list JSON

**Test:**

```bash
./build/bin/ascii-query-server --attach $PID --port 9999 &
curl localhost:9999/process
# {"pid": 12345, "name": "ascii-chat", "state": "running", ...}
```

---

### Commit 4: Variable Reading (Immediate Mode)

**Files:**

```
src/tooling/query/
├── variable_reader.h       # Variable reading interface
├── variable_reader.cpp     # SBFrame/SBValue operations
└── type_formatter.h/cpp    # Type-aware value formatting
```

**Implementation:**

```cpp
// variable_reader.h
struct VariableInfo {
    std::string name;
    std::string type;
    std::string value;
    uint64_t address;
    size_t size;
    std::vector<VariableInfo> members;  // For structs
};

class VariableReader {
public:
    VariableReader(lldb::SBTarget& target);

    // Read variable at file:line (stops target briefly)
    std::optional<VariableInfo> read(
        const std::string& file,
        int line,
        const std::string& var_name,
        int expand_depth = 0
    );

    // Parse member path: "client.socket.fd" or "arr[0].field"
    std::optional<VariableInfo> readMember(
        lldb::SBValue& base,
        const std::string& member_path
    );

private:
    lldb::SBTarget& target_;
};
```

**Deliverables:**

- [ ] Find variable by file:line:name
- [ ] Read primitive types (int, float, char\*, etc.)
- [ ] Read struct members via dot notation
- [ ] Read array elements via bracket notation
- [ ] Handle pointers (auto-dereference for `->`)
- [ ] `GET /query` endpoint working

**Test:**

```bash
curl 'localhost:9999/query?file=src/server.c&line=50&name=options.port'
# {"status": "ok", "result": {"value": "8080", "type": "int", ...}}
```

---

### Commit 5: Breakpoint Mode

**Files:**

```
src/tooling/query/
├── breakpoint_manager.h    # Breakpoint lifecycle management
└── breakpoint_manager.cpp  # LLDB breakpoint operations
```

**Implementation:**

```cpp
// breakpoint_manager.h
struct Breakpoint {
    int id;
    std::string file;
    int line;
    std::string condition;
    int hit_count;
    bool enabled;
};

class BreakpointManager {
public:
    BreakpointManager(lldb::SBTarget& target);

    // Set breakpoint, wait for hit, capture variable, remove breakpoint
    std::optional<VariableInfo> queryAtBreakpoint(
        const std::string& file,
        int line,
        const std::string& var_name,
        int timeout_ms
    );

    // Persistent breakpoints
    int setBreakpoint(const std::string& file, int line,
                      const std::string& condition = "");
    bool removeBreakpoint(int id);
    std::vector<Breakpoint> listBreakpoints() const;

private:
    lldb::SBTarget& target_;
    std::unordered_map<int, lldb::SBBreakpoint> breakpoints_;
};
```

**Deliverables:**

- [ ] Set breakpoint at file:line
- [ ] Wait for breakpoint hit with timeout
- [ ] Capture variable at breakpoint
- [ ] Remove breakpoint after capture
- [ ] `GET /query?mode=breakpoint` working
- [ ] `GET /breakpoints`, `POST /breakpoints`, `DELETE /breakpoints/:id`

**Test:**

```bash
# Query that waits for line to execute
curl 'localhost:9999/query?file=src/server.c&line=200&name=frame&mode=breakpoint&timeout_ms=10000'
# (blocks until line 200 executes, then returns frame value)
```

---

### Commit 6: Auto-Spawn Integration

**Files:**

```
lib/tooling/query/
├── query.h                 # Update with spawn helpers
├── query.c                 # Spawn/attach implementation
└── query_internal.h        # Internal declarations
```

**Implementation:**

```c
// query.h - Public API for target process

// Initialize: spawn controller attached to current process
// Returns port number on success, -1 on failure
int query_init(int preferred_port);

// Shutdown: terminate controller process
void query_shutdown(void);

// Check if controller is running
int query_is_active(void);

// Get controller port (for logging/debugging)
int query_get_port(void);

// Macros that compile out in release
#ifndef NDEBUG
#define QUERY_INIT(port) query_init(port)
#define QUERY_SHUTDOWN() query_shutdown()
#define QUERY_ACTIVE() query_is_active()
#else
#define QUERY_INIT(port) ((void)0, -1)
#define QUERY_SHUTDOWN() ((void)0)
#define QUERY_ACTIVE() (0)
#endif
```

```c
// query.c
int query_init(int preferred_port) {
    pid_t self = getpid();
    char port_str[16], pid_str[16];
    snprintf(port_str, sizeof(port_str), "%d", preferred_port);
    snprintf(pid_str, sizeof(pid_str), "%d", self);

    pid_t child = fork();
    if (child == 0) {
        // Child: exec the controller
        execlp("ascii-query-server", "ascii-query-server",
               "--attach", pid_str,
               "--port", port_str,
               NULL);
        _exit(1);
    }

    g_controller_pid = child;
    g_controller_port = preferred_port;

    // Wait for controller to be ready
    return wait_for_http_ready(preferred_port, 5000) ? preferred_port : -1;
}
```

**Deliverables:**

- [ ] `query_init()` spawns controller as child process
- [ ] `query_shutdown()` terminates controller cleanly
- [ ] Controller finds itself via `argv[0]` path resolution
- [ ] Works in debug builds, compiles out in release

**Test:**

```c
// In test program
QUERY_INIT(9999);
printf("Query server at http://localhost:9999\n");
sleep(60);  // Time to test queries
QUERY_SHUTDOWN();
```

---

### Commit 7: Struct Expansion & Type Formatting

**Files:**

```
src/tooling/query/
├── type_formatter.cpp      # Enhanced type formatting
└── struct_expander.h/cpp   # Recursive struct expansion
```

**Implementation:**

- Recursive struct member enumeration
- Configurable depth limit
- Smart truncation for large arrays
- Special handling for common types (strings, buffers)

**Deliverables:**

- [ ] `expand=true` parameter working
- [ ] `depth=N` parameter working
- [ ] Arrays show first N elements + count
- [ ] Char arrays show as strings
- [ ] Circular reference detection

**Test:**

```bash
curl 'localhost:9999/query?file=src/server.c&line=100&name=client&expand=true&depth=2'
# Returns full struct with nested members
```

---

### Commit 8: CMake Integration

**Files:**

```
cmake/tooling/
├── Query.cmake             # Main integration module
└── FindLLDB.cmake          # LLDB discovery (if not using existing)

CMakeLists.txt              # Root - add option and include
```

**Implementation:**

```cmake
# cmake/tooling/Query.cmake
include_guard(GLOBAL)

option(ASCIICHAT_BUILD_WITH_QUERY "Build query debug tool" OFF)

function(ascii_query_prepare)
    if(NOT ASCIICHAT_BUILD_WITH_QUERY)
        return()
    endif()

    # Only in Debug builds
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(STATUS "Query tool disabled (not Debug build)")
        return()
    endif()

    # Find LLDB
    find_package(LLDB REQUIRED)

    # Build controller executable
    add_subdirectory(src/tooling/query)

    # Build runtime library (for auto-spawn)
    add_library(ascii-query-runtime STATIC
        lib/tooling/query/query.c
    )
    target_include_directories(ascii-query-runtime PUBLIC lib/tooling/query)

    set(ASCII_QUERY_ENABLED TRUE PARENT_SCOPE)
endfunction()

function(ascii_query_finalize)
    if(NOT ASCII_QUERY_ENABLED)
        return()
    endif()

    # Link runtime to main targets
    target_link_libraries(ascii-chat PRIVATE ascii-query-runtime)
    target_compile_definitions(ascii-chat PRIVATE ASCIICHAT_QUERY_ENABLED=1)

    # Ensure controller is built before main app
    add_dependencies(ascii-chat ascii-query-server)
endfunction()
```

**Deliverables:**

- [ ] `ASCIICHAT_BUILD_WITH_QUERY` option
- [ ] LLDB discovery working on all platforms
- [ ] Controller builds as separate executable
- [ ] Runtime library links to main targets
- [ ] Only enabled for Debug builds

**Test:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build
./build/bin/ascii-query-server --help
```

---

### Commit 9: Platform Testing - macOS

**Focus:** Ensure everything works on macOS (your primary dev platform)

**Tasks:**

- [ ] LLDB attachment works (may need code signing)
- [ ] Breakpoints work on ARM64 (Apple Silicon)
- [ ] Breakpoints work on x86_64 (Intel Mac, if available)
- [ ] Auto-spawn works with macOS security restrictions
- [ ] HTTP server handles macOS socket quirks

**Platform-Specific:**

```cpp
#if defined(__APPLE__)
// macOS: May need to handle SIP restrictions
// Debugger attachment requires special entitlements or disabling SIP
// For development: codesign with get-task-allow entitlement
#endif
```

**Test Script:** `tests/scripts/test_query_macos.sh`

```bash
#!/bin/bash
set -e

# Build with query support
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
cmake --build build

# Start test target
./build/bin/ascii-chat server --port 8080 &
TARGET_PID=$!
sleep 1

# Start query controller
./build/bin/ascii-query-server --attach $TARGET_PID --port 9999 &
sleep 1

# Run queries
curl -s localhost:9999/process | jq .
curl -s 'localhost:9999/query?file=src/server/main.c&line=50&name=options' | jq .

# Cleanup
kill $TARGET_PID 2>/dev/null || true
pkill ascii-query-server 2>/dev/null || true

echo "macOS tests passed!"
```

---

### Commit 10: Platform Testing - Linux

**Focus:** Ensure everything works on Linux

**Tasks:**

- [ ] LLDB attachment works (ptrace permissions)
- [ ] Breakpoints work on x86_64
- [ ] Breakpoints work on ARM64 (if available)
- [ ] Auto-spawn works
- [ ] Works in Docker container

**Platform-Specific:**

```cpp
#if defined(__linux__)
// Linux: May need ptrace permissions
// Check /proc/sys/kernel/yama/ptrace_scope
// 0 = permissive, 1 = restricted (default on Ubuntu)
// For Docker: --cap-add=SYS_PTRACE
#endif
```

**Test Script:** `tests/scripts/test_query_linux.sh`

```bash
#!/bin/bash
set -e

# Check ptrace permissions
PTRACE_SCOPE=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo "0")
if [ "$PTRACE_SCOPE" != "0" ]; then
    echo "Warning: ptrace_scope=$PTRACE_SCOPE, may need sudo or: echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope"
fi

# Build and test (similar to macOS script)
# ...
```

**Docker Test:** `tests/docker/Dockerfile.query-test`

```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y \
    lldb liblldb-dev cmake ninja-build clang
# ... build and test
```

---

### Commit 11: Platform Testing - Windows

**Focus:** Ensure everything works on Windows

**Tasks:**

- [ ] LLDB attachment works (Windows debug API)
- [ ] Breakpoints work on x86_64
- [ ] Auto-spawn works (CreateProcess instead of fork)
- [ ] HTTP server works with Winsock
- [ ] Path handling (backslashes, drive letters)

**Platform-Specific:**

```cpp
#if defined(_WIN32)
// Windows: Use CreateProcess instead of fork/exec
// Debug API: DebugActiveProcess, WaitForDebugEvent
// LLDB uses Windows debug API under the hood
#endif
```

**Windows-Specific Implementation:**

```c
// query.c - Windows spawn
#ifdef _WIN32
int query_init(int preferred_port) {
    char cmdline[MAX_PATH];
    snprintf(cmdline, sizeof(cmdline),
             "ascii-query-server.exe --attach %d --port %d",
             GetCurrentProcessId(), preferred_port);

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                        CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
        return -1;
    }

    g_controller_handle = pi.hProcess;
    g_controller_port = preferred_port;

    return wait_for_http_ready(preferred_port, 5000) ? preferred_port : -1;
}
#endif
```

**Test:** Use `build.ps1` and manual testing on Windows

---

### Commit 12: Unit Tests

**Files:**

```
tests/unit/tooling/
├── query_api_test.c        # C API tests
├── query_http_test.cpp     # HTTP endpoint tests
└── query_variable_test.cpp # Variable reading tests
```

**Test Categories:**

1. **API Tests** (query_api_test.c)

   - `QUERY_INIT` / `QUERY_SHUTDOWN` lifecycle
   - Port allocation
   - Process spawn/cleanup

2. **HTTP Tests** (query_http_test.cpp)

   - Endpoint routing
   - JSON serialization
   - Error handling
   - CORS headers

3. **Variable Tests** (query_variable_test.cpp)
   - Primitive type reading
   - Struct member access
   - Array indexing
   - Pointer dereferencing
   - Edge cases (NULL, invalid paths)

**Test Fixture:**

```cpp
// Test target program with known variables
// tests/fixtures/query_test_target.c
int global_counter = 42;

struct TestStruct {
    int id;
    char name[32];
    struct { int x, y; } point;
};

void test_function(void) {
    struct TestStruct data = {1, "test", {10, 20}};
    volatile int local = 123;  // Keep in memory
    // Breakpoint here to test queries
    (void)local;
    (void)data;
}
```

---

### Commit 13: Integration Tests

**Files:**

```
tests/integration/tooling/
├── query_integration_test.sh   # Full workflow tests
└── query_stress_test.sh        # Concurrent query stress test
```

**Integration Test Scenarios:**

1. Start server → attach controller → query → detach → server continues
2. Multiple sequential queries
3. Breakpoint query with timeout
4. Struct expansion at various depths
5. Query during high-load operation
6. Controller crash recovery

---

### Commit 14: Documentation

**Files:**

```
docs/tooling/
├── query-tool.md           # User guide
├── query-api.md            # HTTP API reference
└── query-troubleshooting.md # Common issues

lib/tooling/query/
└── query.h                 # Doxygen comments
```

**Documentation Contents:**

1. **query-tool.md** - User Guide

   - Quick start
   - Launch modes (standalone vs auto-spawn)
   - Example workflows
   - Editor integration (vim, vscode)

2. **query-api.md** - API Reference

   - All HTTP endpoints
   - Request/response formats
   - Error codes

3. **query-troubleshooting.md** - Troubleshooting
   - "Permission denied" (ptrace, code signing)
   - "Cannot attach" (process already being debugged)
   - Platform-specific issues

---

### Commit 15: CI Integration

**Files:**

```
.github/workflows/
└── query-tool.yml          # Query tool CI workflow
```

**CI Jobs:**

```yaml
name: Query Tool

on:
  push:
    paths:
      - "lib/tooling/query/**"
      - "src/tooling/query/**"
      - "cmake/tooling/Query.cmake"

jobs:
  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install LLDB
        run: brew install llvm
      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
          cmake --build build
      - name: Test
        run: ./tests/scripts/test_query_macos.sh

  build-linux:
    runs-on: ubuntu-latest
    container:
      image: ubuntu:22.04
      options: --cap-add=SYS_PTRACE
    steps:
      - uses: actions/checkout@v4
      - name: Install dependencies
        run: apt-get update && apt-get install -y lldb liblldb-dev cmake ninja-build clang
      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
          cmake --build build
      - name: Test
        run: ./tests/scripts/test_query_linux.sh

  build-windows:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install LLVM
        run: choco install llvm
      - name: Build
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Debug -DASCIICHAT_BUILD_WITH_QUERY=ON
          cmake --build build
      - name: Test
        run: ./tests/scripts/test_query_windows.ps1
```

---

### Commit 16: Final Polish & Merge Prep

**Tasks:**

- [ ] Code review all commits
- [ ] Run full test suite on all platforms
- [ ] Update CLAUDE.md with query tool section
- [ ] Update main README.md with query tool mention
- [ ] Squash/rebase if needed for clean history
- [ ] Create PR with full description

**Merge Checklist:**

- [ ] All CI jobs passing (macOS, Linux, Windows)
- [ ] No memory leaks (ASan clean)
- [ ] Documentation complete
- [ ] Code formatted (`cmake --build build --target format`)
- [ ] No compiler warnings
- [ ] Tested manually on at least 2 platforms

---

## Project Structure (Final)

```
ascii-chat/
├── lib/tooling/query/
│   ├── query.h                 # Public C API
│   ├── query.c                 # Auto-spawn implementation
│   └── CMakeLists.txt          # Library build
│
├── src/tooling/query/
│   ├── main.cpp                # Controller entry point
│   ├── lldb_controller.h/cpp   # LLDB wrapper
│   ├── http_server.h/cpp       # HTTP server
│   ├── variable_reader.h/cpp   # Variable reading
│   ├── breakpoint_manager.h/cpp # Breakpoint management
│   ├── type_formatter.h/cpp    # Type formatting
│   ├── json.h                  # JSON helpers
│   ├── CMakeLists.txt          # Executable build
│   └── README.md               # Quick reference
│
├── cmake/tooling/
│   └── Query.cmake             # CMake integration
│
├── tests/
│   ├── unit/tooling/
│   │   ├── query_api_test.c
│   │   ├── query_http_test.cpp
│   │   └── query_variable_test.cpp
│   ├── integration/tooling/
│   │   └── query_integration_test.sh
│   ├── scripts/
│   │   ├── test_query_macos.sh
│   │   ├── test_query_linux.sh
│   │   └── test_query_windows.ps1
│   └── fixtures/
│       └── query_test_target.c
│
├── docs/tooling/
│   ├── QUERY_TOOL_PLAN.md      # This document
│   ├── query-tool.md           # User guide
│   ├── query-api.md            # API reference
│   └── query-troubleshooting.md
│
└── .github/workflows/
    └── query-tool.yml          # CI workflow
```

---

## Timeline Estimate

| Commit | Description         | Complexity |
| ------ | ------------------- | ---------- |
| 1      | Project scaffolding | Low        |
| 2      | LLDB attachment     | Medium     |
| 3      | HTTP server         | Low        |
| 4      | Variable reading    | High       |
| 5      | Breakpoint mode     | High       |
| 6      | Auto-spawn          | Medium     |
| 7      | Struct expansion    | Medium     |
| 8      | CMake integration   | Medium     |
| 9      | macOS testing       | Low        |
| 10     | Linux testing       | Medium     |
| 11     | Windows testing     | High       |
| 12     | Unit tests          | Medium     |
| 13     | Integration tests   | Low        |
| 14     | Documentation       | Low        |
| 15     | CI integration      | Medium     |
| 16     | Final polish        | Low        |

---

## Risk Mitigation

| Risk                              | Mitigation                                             |
| --------------------------------- | ------------------------------------------------------ |
| LLDB API changes between versions | Pin minimum LLDB version, test multiple versions in CI |
| macOS code signing requirements   | Document entitlements, provide signing script          |
| Linux ptrace restrictions         | Document workarounds, test in Docker with SYS_PTRACE   |
| Windows LLDB immaturity           | Have fallback plan using Windows Debug API directly    |
| Performance impact on target      | Make attachment/detachment fast, document overhead     |

---

## Success Criteria

**Minimum Viable:**

- [x] Can query primitive variables on macOS
- [x] HTTP API works
- [ ] Basic documentation

**Full Feature:**

- [~] All query types working (immediate, breakpoint, struct expansion)
  - [x] Immediate mode (query while running)
  - [x] Breakpoint mode (wait for line, capture variable)
  - [ ] Struct expansion (recursive traversal)
- [ ] Works on macOS, Linux, Windows
  - [x] macOS ARM64 tested and working
  - [ ] Linux testing pending
  - [ ] Windows testing pending
- [~] Auto-spawn integration (macros exist, implementation partial)
- [ ] CI passing on all platforms

**Production Ready:**

- [ ] Full test coverage
- [ ] Complete documentation
- [ ] No known bugs
- [ ] Clean merge to master
