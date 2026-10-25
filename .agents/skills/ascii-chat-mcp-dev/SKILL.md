---
name: ascii-chat-mcp-dev
description: Develop and test ascii-chat MCP tools using a native runtime copy, avoiding Windows build file locks and coordinating server restarts across development chats.
---

# ascii-chat MCP development

Read the checkout's AGENTS.md and inspect its current MCP configuration before changing it. Use the worktree containing the implementation under test. Preserve its build directory and shared dependency cache; build with the existing Clang/Ninja default preset.

## Keep the build and MCP executables separate

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

The repository Codex configuration uses the suffixless path, which resolves the `.exe` on Windows and the native executable on Unix:

```toml
[mcp_servers.ascii_chat]
command = "./build/bin/ascii-chat-mcp"
args = ["mcp-server"]
tool_timeout_sec = 70
```

Resolve relative paths against the client's working directory. A local configuration may need an absolute path into the implementation worktree. Preserve unrelated server entries. MCP mode skips update checks automatically. Stdio retains local webcam and filesystem access; HTTP mode intentionally restricts these capabilities.

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

Multiple development chats can each own an MCP server connected to the same worktree. Restarting one can leave another chat with a dead transport or stale tool connection. Before a shared-runtime refresh, determine which chats are affected and coordinate a brief pause. When the user has authorized messaging those chats, use the available thread tools to notify their owners before stopping servers, then tell them when the build and copy are ready. Otherwise report the affected chats to the user; this skill does not independently authorize sending messages.

After stopping the affected servers: finish the build, refresh the runtime copy, and reconnect or restart each affected MCP client. Simply launching another stdio process does not reconnect an existing client's pipes. Inspect the current client's reconnect controls; if the session cannot reload MCP, state that it needs a reconnect or new chat. Do not silently create or archive chats.

Verify through an actual MCP tool call from a connected client. A direct CLI render or hand-written JSON-RPC subprocess probe is useful additional evidence, but does not establish that the client's MCP connection works. Check that the client runs the refreshed copy before attributing stale behavior to the implementation. For rendering changes, compare the artifact with the equivalent CLI output and inspect images or video frames. Report the tests performed and any clients still needing reconnection.
