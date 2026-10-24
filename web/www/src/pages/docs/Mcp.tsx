import { useEffect } from "react";
import { Heading, CodeBlock } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";

export default function Mcp() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "MCP Server", path: "/docs/mcp" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title={pageMetadata.mcp.title}
        description={pageMetadata.mcp.description}
        url={`${SITES.MAIN}/docs/mcp`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              MCP Server
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Render media as ASCII art from an AI assistant or any Model
              Context Protocol client.
            </p>
          </header>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-cyan-400">
              Connect locally over stdio
            </Heading>
            <p className="docs-paragraph">
              Build or install ascii-chat, then configure your MCP client to
              launch the following command. The server uses the same mirror
              rendering pipeline as the CLI. JSON-RPC messages use stdin and
              stdout; logs and diagnostics go to stderr.
            </p>
            <CodeBlock language="bash">{`ascii-chat mcp-server`}</CodeBlock>
            <p className="docs-paragraph">
              For a source checkout, the project configuration uses this entry
              in .codex/config.toml. Build the executable before starting a new
              chat. The suffixless path resolves ascii-chat.exe on Windows and
              ascii-chat on Unix; use an absolute path when the checkout is
              elsewhere.
            </p>
            <CodeBlock language="text">{`[mcp_servers.ascii_chat]
command = "./build/bin/ascii-chat"
args = ["--no-check-update", "mcp-server"]
tool_timeout_sec = 70`}</CodeBlock>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              Four rendering tools
            </Heading>
            <div className="overflow-x-auto">
              <table className="w-full text-left">
                <thead>
                  <tr>
                    <th className="p-3">Tool</th>
                    <th className="p-3">Input</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td className="p-3">
                      <code className="whitespace-nowrap">render_file</code>
                    </td>
                    <td className="p-3">
                      Exactly one of path or data_base64, with optional
                      mime_type. Synthetic test media can omit both.
                    </td>
                  </tr>
                  <tr>
                    <td className="p-3">
                      <code className="whitespace-nowrap">render_url</code>
                    </td>
                    <td className="p-3">
                      A url supported by the CLI FFmpeg/yt-dlp resolver.
                    </td>
                  </tr>
                  <tr>
                    <td className="p-3">
                      <code className="whitespace-nowrap">render_webcam</code>
                    </td>
                    <td className="p-3">
                      An optional integer device (0–10). Local stdio only.
                    </td>
                  </tr>
                  <tr>
                    <td className="p-3">
                      <code className="whitespace-nowrap">
                        render_visualization
                      </code>
                    </td>
                    <td className="p-3">
                      visualization: waveform or fft. Audio type: test
                      (default), file (path or base64), url, or local microphone
                      with optional device.
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <p className="docs-paragraph">
              All tools accept options with CLI long names without leading
              dashes, plus timeout_ms (1,000–60,000; default 45,000). Local
              tools also accept output_path. Use color-mode for rendered colors;
              color controls CLI diagnostics. Defaults are 100×40 characters,
              monochrome, and a single snapshot.
            </p>
            <p className="docs-paragraph">
              These are tools/call parameters for an image snapshot:
            </p>
            <CodeBlock language="json">
              {JSON.stringify(
                {
                  name: "render_file",
                  arguments: {
                    path: "/home/me/photo.png",
                    options: {
                      width: 100,
                      height: 40,
                      "color-mode": "truecolor",
                      snapshot: true,
                      "snapshot-delay": 0,
                    },
                  },
                },
                null,
                2,
              )}
            </CodeBlock>
            <p className="docs-paragraph">
              Uploaded attachments must be read and base64-encoded by the MCP
              client into data_base64. Attaching a file to a chat does not
              automatically give the server access. File paths and camera
              devices refer to the computer running the server.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-orange-400">
              Save images and videos
            </Heading>
            <p className="docs-paragraph">
              Set output_path to a filename whose extension selects the output
              format, such as PNG or MP4. The parent directory must exist.
              Existing files and symlinks are never overwritten, including when
              concurrent requests choose the same destination. Saving requires
              filesystem hard-link support.
            </p>
            <p className="docs-paragraph">
              Set render-font-size to 28 for larger images or videos. Width and
              height control the character grid, while font size controls its
              pixel size. Use snapshot-delay above zero to capture a clip after
              the first frame. Use seek to select a timestamp in source media.
            </p>
            <CodeBlock language="json">
              {JSON.stringify(
                {
                  name: "render_visualization",
                  arguments: {
                    visualization: "fft",
                    audio: { type: "test" },
                    options: {
                      width: 80,
                      height: 24,
                      "color-mode": "truecolor",
                      "render-font-size": 28,
                      snapshot: true,
                      "snapshot-delay": 2,
                      fps: 10,
                    },
                    output_path: "/home/me/fft.mp4",
                  },
                },
                null,
                2,
              )}
            </CodeBlock>
            <p className="docs-paragraph">
              The response includes text content and structuredContent with
              ascii, elapsed_ms, and saved_files (absolute paths). Rendering
              failures set isError. Each request runs in an isolated mirror
              child process with explicit options, deadlines, cancellation, and
              output limits. User and system configuration is not loaded in that
              child.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-purple-400">
              Serve Streamable HTTP
            </Heading>
            <CodeBlock language="bash">{`ascii-chat mcp-server --server --address 127.0.0.1 --port 27228
# IPv4 and IPv6 bind addresses
ascii-chat mcp-server --server 0.0.0.0 ::1 --port 28028`}</CodeBlock>
            <p className="docs-paragraph">
              Connect your HTTP MCP client to http://127.0.0.1:27228/mcp. The
              default port is 27228; --port or ASCII_CHAT_PORT overrides it.
              Bind addresses use --address, --address6, or up to two positional
              addresses. Existing --websocket-tls-cert, --websocket-tls-key, and
              --websocket-tls-key-password options configure HTTPS.
              --port-forwarding enables UPnP; --max-clients limits render
              concurrency, up to four jobs.
            </p>
            <p className="docs-paragraph">
              HTTP mode omits render_webcam and rejects caller-selected
              filesystem paths, microphones, output_path, custom font paths, and
              arbitrary yt-dlp options. Uploaded bytes and media URLs remain
              available. These restrictions do not sandbox media decoders:
              public hosting requires an unprivileged sandbox without secrets or
              device mounts, and egress rules blocking private, loopback,
              link-local, and metadata destinations, including redirects and
              nested media requests. Add authentication and rate limits at your
              reverse proxy as needed.
            </p>
            <p className="docs-paragraph">
              The allowed browser Origin defaults to https://mcp.ascii-chat.com;
              change it with --origin. Native clients may omit Origin. POST /mcp
              serves stateless JSON responses; GET and DELETE return 405.
              Clients send Accept including application/json and
              text/event-stream. Supported protocol versions are 2025-03-26,
              2025-06-18, and 2025-11-25. Decoded uploads are limited to 16 MiB,
              request bodies to 24 MiB, and returned ASCII to 8 MiB.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-yellow-400">
              Logs and diagnostics
            </Heading>
            <CodeBlock language="bash">{`ascii-chat --log-level debug --grep 'MCP|render' mcp-server --server --status-screen`}</CodeBlock>
            <p className="docs-paragraph">
              HTTP status uses the shared terminal UI, including the log viewer,
              error stacks, error popups, and sync primitives. Debug builds
              support --sync-state, --backtrace, and memory reports. Stdio
              reserves stdin for the protocol and does not consume terminal
              shortcuts. Keep a bounded snapshot duration for webcam or
              synthetic sources; unbounded capture ends with a timeout error.
            </p>
            <p className="docs-paragraph">
              See{" "}
              <a href="/man1" className="link-standard">
                ascii-chat(1)
              </a>{" "}
              for CLI flags and{" "}
              <a href="/docs/media" className="link-standard">
                Media Files and URLs
              </a>{" "}
              for the underlying renderer and export formats.
            </p>
          </section>
          <Footer />
        </div>
      </div>
    </>
  );
}
