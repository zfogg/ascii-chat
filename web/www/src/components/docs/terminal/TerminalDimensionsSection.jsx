import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function TerminalDimensionsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        📏 Terminal Dimensions
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Auto-Detection & Environment Variables
        </Heading>
        <p className="docs-paragraph">
          Terminal dimensions are automatically detected via:
        </p>
        <ul className="list-disc list-inside text-gray-300 text-sm space-y-1 ml-2">
          <li>
            Terminal queries (ioctl TIOCGWINSZ on Unix, Console API on Windows)
          </li>
          <li>
            <code>$COLUMNS</code> and <code>$ROWS</code> environment variables
            (when set)
          </li>
          <li>Fallback defaults: 110×70 if detection fails</li>
        </ul>
        <CodeBlock language="bash">
          {
            "# Set dimensions via environment variables\nexport COLUMNS=120 ROWS=40\nascii-chat client example.com\n\n# Or set just for this command\nCOLUMNS=150 ROWS=50 ascii-chat mirror video.mp4"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Auto-Update:</strong> When running interactively (not
            piped), dimensions are updated in real-time when you resize the
            terminal.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Custom Dimensions
        </Heading>
        <p className="docs-paragraph">
          Override auto-detection with explicit width and height:
        </p>
        <CodeBlock language="bash">
          {
            "# Set custom dimensions\nascii-chat client --width 120 --height 40\n\n# Short form\nascii-chat client -x 120 -y 40\n\n# Very wide for multiple columns\nascii-chat mirror -x 200 -y 60\n\n# Minimal size\nascii-chat client -x 20 -y 10\n\n# Via environment variables\nexport ASCII_CHAT_WIDTH=150 ASCII_CHAT_HEIGHT=50"
          }
        </CodeBlock>
        <div className="info-box-note mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Valid Range:</strong> width 20-512, height 10-256
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Piping & Non-Interactive Mode
        </Heading>
        <p className="docs-paragraph">
          When output is piped (<code>ascii-chat mirror | tee file.txt</code>
          ), dimension detection changes:
        </p>
        <ul className="list-disc list-inside text-gray-300 text-sm space-y-1 ml-2">
          <li>
            Terminal queries fail (<code>isatty()</code> returns false)
          </li>
          <li>
            Falls back to <code>$COLUMNS</code> and <code>$ROWS</code>
          </li>
          <li>Uses defaults (110×70) if env vars not set</li>
          <li>
            Always use <code>-x</code>/<code>-y</code> for consistent output
          </li>
        </ul>
        <CodeBlock language="bash">
          {
            "# Capture single frame to file with explicit size\nascii-chat mirror -x 120 -y 40 --snapshot -D 0 > frame.txt\n\n# Pipe to file with consistent dimensions\nascii-chat mirror -x 100 -y 30 | tee output.txt\n\n# Use with COLUMNS/ROWS for piped output\nCOLUMNS=200 ROWS=60 ascii-chat mirror | cat"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
