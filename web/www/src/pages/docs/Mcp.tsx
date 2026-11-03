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
              Connect your assistant
            </Heading>
            <p className="docs-paragraph">
              Install ascii-chat and make sure the command is on your PATH. Add
              a configuration below to your assistant, then restart it or
              reconnect the server. Your assistant starts the server as needed;
              no network port is opened. Use the full executable path if needed.
            </p>
            <CodeBlock language="bash">{`ascii-chat mcp-server --transport stdio`}</CodeBlock>
            <p className="docs-paragraph">
              Add this Codex configuration to ~/.codex/config.toml:
            </p>
            <CodeBlock language="text">{`[mcp_servers.ascii_chat]
command = "ascii-chat"
args = ["mcp-server", "--transport", "stdio"]
tool_timeout_sec = 70`}</CodeBlock>
            <Heading level={3} className="text-xl font-semibold mb-3">
              Claude Code configuration
            </Heading>
            <p className="docs-paragraph">
              Add this to your project’s .mcp.json and approve the server when
              prompted. Use /mcp to inspect the connection:
            </p>
            <CodeBlock language="json">
              {JSON.stringify(
                {
                  mcpServers: {
                    ascii_chat: {
                      type: "stdio",
                      command: "ascii-chat",
                      args: ["mcp-server", "--transport", "stdio"],
                    },
                  },
                },
                null,
                2,
              )}
            </CodeBlock>
            <Heading level={3} className="text-xl font-semibold mb-3">
              OpenCode configuration
            </Heading>
            <p className="docs-paragraph">Add this to opencode.json:</p>
            <CodeBlock language="json">
              {JSON.stringify(
                {
                  $schema: "https://opencode.ai/config.json",
                  mcp: {
                    ascii_chat: {
                      type: "local",
                      command: [
                        "ascii-chat",
                        "mcp-server",
                        "--transport",
                        "stdio",
                      ],
                      enabled: true,
                      timeout: 70000,
                    },
                  },
                },
                null,
                2,
              )}
            </CodeBlock>
            <p className="docs-paragraph">
              Once connected, ask your assistant to render a picture, a video
              frame, a webcam view, or an audio visualization as ASCII art.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              Available tools
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
                      Optional video_device (default 0), audio_device (default
                      system microphone), and volume (0–1 microphone gain).
                      Timed exports include microphone audio unless
                      options.audio is false. Unavailable with --server.
                    </td>
                  </tr>
                  <tr>
                    <td className="p-3">
                      <code>device_list</code>
                    </td>
                    <td className="p-3">
                      No arguments. Returns named video_devices and
                      audio_devices with index and is_default, plus
                      default_video_device and default_audio_device. Hidden with
                      --server.
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
                      with optional audio_device and volume.
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <p className="docs-paragraph">
              Rendering tools accept options with CLI long names without leading
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
              The response includes the ASCII result and paths of saved files.
              Use a new filename for each export.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-purple-400">
              Serve Streamable HTTP
            </Heading>
            <p className="docs-paragraph">
              Add --transport http to select networked Streamable HTTP. Run this
              process independently, then configure your MCP client with its
              endpoint URL instead of a process launch command.
            </p>
            <CodeBlock language="bash">{`ascii-chat mcp-server --transport http --address 127.0.0.1 --port 27228
# IPv4 and IPv6 bind addresses
ascii-chat mcp-server --transport http 0.0.0.0 ::1 --port 28028`}</CodeBlock>
            <p className="docs-paragraph">
              Connect your HTTP MCP client to http://127.0.0.1:27228/mcp. The
              default listener is loopback on port 27228; --port or
              ASCII_CHAT_PORT overrides the port. For remote clients, bind to
              0.0.0.0 and use the server hostname or IP in the client URL. Bind
              addresses use --address, --address6, or up to two positional
              addresses. The --http-tls-cert, --http-tls-key, and
              --http-tls-key-password options configure HTTPS. --port-forwarding
              enables UPnP; --max-clients limits render concurrency, up to four
              jobs.
            </p>
            <p className="docs-paragraph">
              Local HTTP retains all five tools. Add --server for cloud
              deployment; it omits render_webcam and rejects caller-selected
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
              If a browser client cannot connect, configure its allowed origin
              with --http-origin. Native MCP clients may omit an Origin header.
            </p>
          </section>
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-yellow-400">
              Troubleshooting
            </Heading>
            <CodeBlock language="bash">{`ascii-chat --log-level debug --grep 'MCP|render' mcp-server --transport http --status-screen`}</CodeBlock>
            <p className="docs-paragraph">
              If your assistant cannot start the server, check that ascii-chat
              is installed and on its PATH, or use an absolute executable path.
              File paths and devices belong to the computer running the server.
              Keep webcam and synthetic captures bounded to avoid timeouts.
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
