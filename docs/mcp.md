# MCP rendering server

`ascii-chat --no-check-update mcp-server` exposes four rendering tools over newline-delimited JSON-RPC on stdio. It runs through the ordinary application initialization, options, logging, error-stack and diagnostic workers. Protocol stdout is reserved before initialization; incidental terminal diagnostics go to stderr.

`ascii-chat --no-check-update mcp-server --server` serves the restricted tool set at `/mcp` using MCP Streamable HTTP and libwebsockets. The default listener is `127.0.0.1:27228`, following the native TCP and WebSocket defaults (27224–27227). Override it with `--port`, `ASCII_CHAT_PORT`, `--address`, `--address6`, or the existing zero-to-two positional IPv4/IPv6 bind addresses. Examples:

```sh
ascii-chat --no-check-update mcp-server --server --port 28028 --address 127.0.0.1
ascii-chat --no-check-update mcp-server --server 0.0.0.0 ::1 --port 27228
ascii-chat --no-check-update --log-level debug --grep 'MCP|render' mcp-server --server --status-screen
```

The existing `--websocket-tls-cert`, `--websocket-tls-key`, and `--websocket-tls-key-password` configure HTTPS for this HTTP listener too. `--port-forwarding` maps the HTTP port with the existing UPnP implementation; `--max-clients` bounds concurrent render jobs (hard maximum four). HTTP status uses the shared terminal controller and log viewer, including error popups and debug controls. Debug builds retain `--sync-state`, `--backtrace`, and memory diagnostics. Stdio does not consume terminal keys: stdin is the protocol stream.

For mcp.ascii-chat.com, terminate HTTPS and enforce rate/body/concurrency limits at a reverse proxy. The configured browser Origin defaults to `https://mcp.ascii-chat.com`; change it with `--origin`. Requests without Origin are accepted for native MCP clients. The initial implementation uses stateless JSON responses; POST handles requests and notifications, GET/DELETE return 405. It supports MCP versions 2025-03-26, 2025-06-18, and 2025-11-25. Clients send Accept including application/json and text/event-stream.

## Tools

- `render_file`: exactly one of `path` or `data_base64`, optional `mime_type`. A source may be omitted when options select synthetic test video/audio.
- `render_url`: `url` for media supported by the CLI FFmpeg/yt-dlp resolver.
- `render_webcam`: optional integer `device` (0–10), local only. `options.test-pattern` uses synthetic video instead.
- `render_visualization`: `visualization` is `waveform` or `fft`; optional `audio` selects `{ "type": "test" }` (default), `{ "type": "file", "path": "..." }`, `{ "type": "file", "data_base64": "...", "mime_type": "audio/wav" }`, `{ "type": "url", "url": "..." }`, or local `{ "type": "microphone", "device": 0 }`.

All tools accept `options`, `timeout_ms` (1000–60000, default 45000), and a local-only optional `output_path`.

```json
{
  "name": "render_file",
  "arguments": {
    "path": "/home/me/image.png",
    "options": {
      "width": 100,
      "height": 40,
      "color-mode": "none",
      "snapshot": true,
      "snapshot-delay": 0
    }
  }
}
```

For uploaded media, the client must read and base64-encode the bytes; an attachment in a conversation is not automatically accessible to an MCP server. This uses normal JSON tool arguments with no upload endpoint or public URL requirement. Decoded uploads are limited to 16 MiB; request bodies to 24 MiB; returned ASCII to 8 MiB.

The shared option schema is generated from the CLI registry through an explicit exposure table in `src/mcp-server/options.c`. It covers palette/characters, color/filter/render mode, dimensions, transforms, effects, FPS, snapshot/delay, seek/loop, test pattern/audio, audio processing, and render styling. Local mode additionally exposes custom render fonts and yt-dlp options. Unsupported/unknown arguments are rejected, including arguments hidden from the hosted schema.

Defaults are 100×40, monochrome, snapshot enabled with delay zero, splash and speaker playback disabled. `snapshot-delay` measures capture duration after the first rendered frame, matching CLI semantics; it is not time from receipt of the tool call. Use `seek` to choose a media timestamp. Snapshot false is bounded by timeout and suitable for finite media; unbounded capture must finish via snapshot or returns a timeout error.

## Saved images and videos

Set `options.render-font-size` (for example, 28) to increase the pixel size of images or videos; `width` and `height` set the character grid. Use `color-mode` for rendered colors, rather than the CLI diagnostic `color` option.

Set local `output_path` to an image/video filename. The existing render-file pipeline renders the ASCII grid into the encoded artifact, with format selected by extension. Parent directories must exist. The server stages the encoded artifact in a private directory beside the destination, then publishes with an atomic non-replacing hard link. Existing regular files, symlinks, and destinations won by concurrent writers are preserved. Filesystems without hard-link support return an error instead of falling back to an overwriting rename. Cancellation/failure removes staged artifacts; successful results contain absolute paths in `structuredContent.saved_files`.

```json
{
  "name": "render_visualization",
  "arguments": {
    "visualization": "fft",
    "audio": { "type": "test" },
    "options": { "snapshot": true, "snapshot-delay": 2, "render-font-size": 28, "color-mode": "truecolor" },
    "output_path": "/home/me/fft.mp4"
  }
}
```

Results contain `content` text plus `structuredContent` with `ascii`, `elapsed_ms`, and `saved_files`. Decoder/render errors use `isError: true`; JSON-RPC errors cover malformed protocol requests. Each render is isolated in a child mirror process with an explicit environment and no user/system configuration. The parent handles deadlines, output limits, and local cancellation while keeping its transport responsive.

## Hosted isolation

Server mode omits webcam tooling and rejects caller-selected paths, microphone access, saved output paths, custom font paths, and arbitrary yt-dlp options. Uploaded bytes use private server-created temporary files. These capability restrictions do not sandbox third-party media decoders or URL extractors: public deployments must run under an unprivileged account/container with no secrets or device mounts and network egress rules blocking private/loopback/link-local/metadata destinations. Enforce those rules on redirected and nested media requests too. Put authentication/rate limits at the reverse proxy if required. Local mode intentionally permits the filesystem and devices of the user running it.

## Codex connection

Register a built executable in the project's `.codex/config.toml`:

```toml
[mcp_servers.ascii_chat]
command = "./build/bin/ascii-chat"
args = ["--no-check-update", "mcp-server"]
tool_timeout_sec = 70
```

The suffixless executable path works on Windows (which resolves `.exe`) and Unix (which uses the exact filename), without a symlink. For a checkout outside the current project, use its absolute suffixless path. Newly created chats can discover the four tools through this stdio connection. The server must be built before starting the chat.
