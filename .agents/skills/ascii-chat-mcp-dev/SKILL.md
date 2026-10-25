---
name: ascii-chat-mcp-dev
description: Develop ascii-chat MCP tools through a shared local HTTP endpoint, avoiding Windows file locks and coordinating server restarts across chats.
---

# ascii-chat MCP development

Read the checkout's AGENTS.md and inspect its current MCP configuration before changing it. Use the worktree containing the implementation under test. Preserve its build directory and shared dependency cache; build with the existing Clang/Ninja default preset.

## Keep the build and MCP executables separate

Development chats now share one independently running HTTP server. Launch the native runtime with `build/bin/ascii-chat-mcp mcp-server --transport http --address 127.0.0.1 --port 27228 --status-screen=false` (add `.exe` on Windows). Keep this URL stable across rebuilds: `http://127.0.0.1:27228/mcp`. Check the port owner before starting or stopping the server; never replace an unrelated listener.

Keep both executables in `build/bin/`:

- `ascii-chat.exe` on Windows, `ascii-chat` on Unix: normal CMake build output.
- `ascii-chat-mcp.exe` on Windows, `ascii-chat-mcp` on Unix: runtime copy used by the development MCP client.

Use a real copy, not a symlink or hard link: Windows can lock the underlying executable even through another name. Keeping the runtime beside the build output preserves dependency lookup and lets the native process access the webcam. Containers require separate device access and are not the default development runtime.

After a successful build, with servers using the runtime copy stopped, refresh it from the repository root:

```powershell
Copy-Item -LiteralPath build/bin/ascii-chat.exe -Destination build/bin/ascii-chat-mcp.exe
# Keep symbols beside the renamed executable when the build provides them.
if (Test-Path -LiteralPath build/bin/ascii-chat.pdb) {
    Copy-Item -LiteralPath build/bin/ascii-chat.pdb -Destination build/bin/ascii-chat-mcp.pdb
}
```

On Unix:

```sh
cp build/bin/ascii-chat build/bin/ascii-chat-mcp
```

The repository Codex configuration uses the shared URL:

```toml
[mcp_servers.ascii_chat]
url = "http://127.0.0.1:27228/mcp"
tool_timeout_sec = 70
```

Preserve unrelated server entries. MCP mode skips update checks automatically. `--transport http` retains webcam and filesystem access; keep the unrestricted listener on loopback. Add `--server` only for cloud capability restrictions. `--transport stdio` explicitly selects the default stdio transport. The project `.mcp.json` also uses the shared HTTP URL.

## Handle executable and DLL locks

The copied executable avoids locking the normal executable, but both processes still load DLLs from `build/bin/`. A rebuild of `asciichat.dll` or another loaded DLL can require stopping every process loading that exact file. The runtime copy is also locked while it runs. Do not promise that copying the executable eliminates all rebuild locks, and do not retry a failed link indefinitely.

Before a rebuild that replaces loaded DLLs, identify affected processes by absolute executable path, command line, and PID. For example, inspect Windows processes without stopping them:

```powershell
Get-CimInstance Win32_Process |
    Where-Object { $_.Name -in @('ascii-chat.exe', 'ascii-chat-mcp.exe') } |
    Select-Object ProcessId, ExecutablePath, CommandLine
```

Only stop processes belonging to the relevant worktree and development task. Never kill all ascii-chat processes by name. If another process still holds the DLL, inspect its loaded module paths rather than assuming it belongs to this checkout. Prefer the client's disconnect or shutdown mechanism; use targeted process termination when necessary and authorized.

## Coordinate across chats

Multiple development chats share this HTTP endpoint. Coordinate one restart owner and a brief pause, preferably without active renders. When the user has authorized messaging affected chats, notify their owners before downtime and after readiness. Otherwise report affected chats to the user; this skill does not independently authorize sending messages.

After stopping the relevant server: finish the build successfully, refresh the runtime copy, and restart the HTTP server on the same address, port, and `/mcp` path. On Windows use `Start-Process -WindowStyle Hidden` with separate stdout/stderr logs under build/. Verify readiness with initialize, tools/list, and a bounded render. HTTP clients retain their URL; in-flight requests may fail during downtime and need retry. Confirm client recovery instead of promising seamless restarts. Schema changes may require tool rediscovery; existing stdio chats need a one-time configuration reload to migrate to HTTP. Reconnect clients only when necessary. Do not silently create or archive chats.

Verify through an actual MCP tool call from a connected client. A direct CLI render or hand-written JSON-RPC subprocess probe is useful additional evidence, but does not establish that the client's MCP connection works. Check that the client runs the refreshed copy before attributing stale behavior to the implementation. For rendering changes, compare the artifact with the equivalent CLI output and inspect images or video frames. Report the tests performed and any clients still needing reconnection.
