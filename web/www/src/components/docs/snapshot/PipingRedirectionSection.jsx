import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function PipingRedirectionSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🔌 Piping, Redirection & isatty()
      </Heading>

      <p className="docs-paragraph">
        ascii-chat automatically detects whether stdout is connected to a
        terminal (TTY) or being piped/redirected. This enables clean
        output for scripting:
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          How Output Detection Works
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-cyan">
            <Heading
              level={4}
              className="text-cyan-300 font-semibold mb-2"
            >
              Terminal (TTY)
            </Heading>
            <p className="text-gray-400 text-sm">
              When stdout is a terminal, ascii-chat outputs with ANSI
              codes, cursor movement, and terminal optimizations for live
              display
            </p>
          </div>
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-2"
            >
              Piped/Redirected (Non-TTY)
            </Heading>
            <p className="text-gray-400 text-sm">
              When stdout is piped or redirected to a file, ascii-chat
              automatically outputs clean ASCII without ANSI codes, cursor
              movement, or RLE compression
            </p>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Practical Examples
        </Heading>
        <CodeBlock language="bash">
          {
            "# Terminal: displayed with colors and animations\nascii-chat mirror --snapshot\n\n# File: clean ASCII saved to disk\nascii-chat mirror --snapshot > frame.txt\n\n# Pipe: clean ASCII sent downstream\nascii-chat mirror --snapshot | pbcopy\n\n# Viewing piped output\nascii-chat mirror --snapshot | cat -\n\n# Force plain ASCII (no color codes at all)\nascii-chat mirror --snapshot --color-mode mono > plain.txt"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Clipboard Magic (macOS)
        </Heading>
        <CodeBlock language="bash">
          {
            "# Capture to clipboard\nascii-chat mirror --snapshot | pbcopy\n\n# Display clipboard contents\npbpaste | cat -\n\n# Capture AND display\nascii-chat mirror --snapshot | tee /tmp/frame.txt | pbcopy\n\n# Capture from clipboard, modify, save back\npbpaste > frame.txt\nnvim frame.txt  # Edit in neovim\ncat frame.txt | pbcopy  # Copy modified version back"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Color Snapshots
        </Heading>
        <p className="docs-paragraph">
          By default, colors are only enabled when stdout is connected to
          a terminal. When piping or redirecting (when{" "}
          <code className="text-cyan-300">isatty()</code> returns false),
          colors are automatically disabled to keep output clean. To
          capture colored snapshots when piping or redirecting, explicitly
          enable colors with{" "}
          <code className="text-cyan-300">--color</code> or{" "}
          <code className="text-cyan-300">--color=true</code>:
        </p>
        <CodeBlock language="bash">
          {
            "# Terminal: colors enabled by default\nascii-chat mirror --snapshot\n\n# Piped/redirected: colors disabled by default (clean ASCII)\nascii-chat mirror --snapshot > frame.txt\nascii-chat mirror --snapshot | pbcopy\n\n# Piped/redirected: enable colors explicitly\nascii-chat mirror --snapshot --color > frame_color.txt\nascii-chat mirror --snapshot --color | pbcopy\n\n# Specify color mode (--color-mode auto is the default)\nascii-chat mirror --snapshot --color --color-mode 256 > frame_256.txt\nascii-chat mirror --snapshot --color --color-mode truecolor > frame_true.txt\n\n# Force plain ASCII (no color codes)\nascii-chat mirror --snapshot --color-mode mono > plain.txt"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
