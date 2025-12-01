# Query Tool HTTP API Reference

This document describes all HTTP endpoints provided by the `ascii-query-server` controller.

## Base URL

```
http://localhost:{port}/
```

Default port is 9999, configurable via `--port` flag.

---

## Endpoints

### `GET /` - Status Page

Returns an HTML status page with process info and interactive query form.

**Response**: `text/html`

---

### `GET /process` - Process Information

Returns information about the attached target process.

**Response**:
```json
{
  "pid": 12345,
  "name": "ascii-chat",
  "state": "running",
  "triple": "arm64-apple-macosx15.0.0",
  "num_threads": 4
}
```

| Field | Type | Description |
|-------|------|-------------|
| `pid` | integer | Process ID |
| `name` | string | Executable name |
| `state` | string | `"running"`, `"stopped"`, or `"exited"` |
| `triple` | string | Target architecture triple |
| `num_threads` | integer | Number of threads |

---

### `GET /threads` - List Threads

Returns all threads in the target process.

**Response**:
```json
{
  "threads": [
    {
      "id": 1,
      "index": 0,
      "name": "main",
      "state": "stopped",
      "stop_reason": "breakpoint",
      "frame": {
        "function": "process_clients",
        "file": "src/server.c",
        "line": 100
      }
    }
  ]
}
```

---

### `GET /frames` - Stack Frames

Returns the call stack for the current thread (when stopped).

**Response**:
```json
{
  "frames": [
    {
      "index": 0,
      "function": "process_clients",
      "file": "src/server.c",
      "line": 100,
      "module": "ascii-chat"
    },
    {
      "index": 1,
      "function": "main",
      "file": "src/server/main.c",
      "line": 50,
      "module": "ascii-chat"
    }
  ]
}
```

**Error** (target not stopped):
```json
{
  "status": "error",
  "error": "not_stopped",
  "message": "Target must be stopped to get stack frames"
}
```

---

### `GET /query` - Query Variable

Read a variable's value. Behavior depends on target state and parameters.

**Parameters**:

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `file` | When running | - | Source file path (relative to project root) |
| `line` | When running | - | Line number |
| `name` | Yes | - | Variable name (supports dot/arrow notation) |
| `break` | No | - | If present, stop at breakpoint and stay stopped |
| `timeout` | No | `5000` | Timeout in ms waiting for breakpoint |
| `expand` | No | `false` | Expand struct members recursively |
| `depth` | No | `3` | Max recursion depth for expansion |
| `frame` | No | `0` | Stack frame index or function name |
| `thread` | No | current | Thread ID to query |

**Examples**:

```bash
# Query variable at specific location (immediate mode)
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count'

# Query with breakpoint (interactive mode)
curl 'localhost:9999/query?file=src/server.c&line=100&name=client_count&break'

# Query while already stopped (no file:line needed)
curl 'localhost:9999/query?name=client.socket.fd'

# Expand struct
curl 'localhost:9999/query?name=client&expand&depth=2'

# Query from parent frame
curl 'localhost:9999/query?name=argc&frame=1'
```

**Response** (immediate mode):
```json
{
  "status": "ok",
  "result": {
    "name": "client_count",
    "value": "5",
    "type": "int",
    "address": "0x7fff5fbff8ac",
    "size": 4
  }
}
```

**Response** (breakpoint mode):
```json
{
  "status": "ok",
  "stopped": true,
  "breakpoint": {
    "file": "src/server.c",
    "line": 100,
    "hit_count": 1
  },
  "result": {
    "name": "client_count",
    "value": "5",
    "type": "int"
  }
}
```

**Response** (struct expansion):
```json
{
  "status": "ok",
  "result": {
    "name": "client",
    "type": "client_t",
    "value": "{...}",
    "children": [
      {"name": "id", "type": "uint32_t", "value": "42"},
      {"name": "socket", "type": "socket_info_t", "children": [
        {"name": "fd", "type": "int", "value": "7"},
        {"name": "address", "type": "char[64]", "value": "\"127.0.0.1\""}
      ]}
    ]
  }
}
```

---

### `POST /stop` - Stop Target

Stops (pauses) the target process.

**Response**:
```json
{
  "status": "stopped"
}
```

---

### `POST /continue` - Continue Execution

Resumes target execution if stopped.

**Response**:
```json
{
  "status": "running"
}
```

---

### `POST /step` - Single Step

Execute one source line (when stopped).

**Parameters**:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `over` | `false` | Step over (don't enter functions) |
| `out` | `false` | Step out (run until function returns) |

**Examples**:
```bash
curl -X POST 'localhost:9999/step'           # Step into
curl -X POST 'localhost:9999/step?over=true' # Step over
curl -X POST 'localhost:9999/step?out=true'  # Step out
```

**Response**:
```json
{
  "status": "stopped",
  "location": {
    "function": "process_clients",
    "file": "src/server.c",
    "line": 101
  }
}
```

---

### `GET /breakpoints` - List Breakpoints

Returns all set breakpoints.

**Response**:
```json
{
  "breakpoints": [
    {
      "id": 1,
      "file": "src/server.c",
      "line": 100,
      "enabled": true,
      "hit_count": 3,
      "condition": ""
    }
  ]
}
```

---

### `POST /breakpoints` - Set Breakpoint

Set a persistent breakpoint.

**Parameters**:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `file` | Yes | Source file path |
| `line` | Yes | Line number |
| `condition` | No | Conditional expression |

**Example**:
```bash
curl -X POST 'localhost:9999/breakpoints?file=src/server.c&line=100&condition=client_count>5'
```

**Response**:
```json
{
  "id": 1,
  "file": "src/server.c",
  "line": 100,
  "enabled": true
}
```

---

### `DELETE /breakpoints` - Remove Breakpoint

Remove a breakpoint by ID.

**Parameters**:

| Parameter | Required | Description |
|-----------|----------|-------------|
| `id` | Yes | Breakpoint ID |

**Example**:
```bash
curl -X DELETE 'localhost:9999/breakpoints?id=1'
```

**Response**:
```json
{
  "status": "ok",
  "removed": 1
}
```

---

### `POST /detach` - Detach from Target

Cleanly detach LLDB from target. Target continues running.

**Response**:
```json
{
  "status": "detached"
}
```

---

## Error Responses

All errors return JSON with `status: "error"`:

```json
{
  "status": "error",
  "error": "error_code",
  "message": "Human-readable description"
}
```

**Error Codes**:

| Code | Description |
|------|-------------|
| `not_stopped` | Operation requires target to be stopped |
| `not_running` | Operation requires target to be running |
| `timeout` | Breakpoint not hit within timeout |
| `variable_not_found` | Variable not found in scope |
| `invalid_parameter` | Missing or invalid parameter |
| `no_debug_info` | File/line has no debug symbols |
| `attach_failed` | Could not attach to process |

---

## HTTP Headers

**Request Headers** (optional):
- `Accept: application/json` - Request JSON response

**Response Headers**:
- `Content-Type: application/json` - JSON responses
- `Access-Control-Allow-Origin: *` - CORS enabled for browser access

---

## Status Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Bad request (missing/invalid parameters) |
| 404 | Endpoint not found |
| 500 | Internal error |
| 503 | Target not attached |
