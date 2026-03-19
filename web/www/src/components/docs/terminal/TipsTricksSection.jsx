import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../../TrackedLink";

export default function TipsTricksSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        💡 Tips & Tricks
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Environment Variable Precedence
        </Heading>
        <p className="docs-paragraph">
          Order of precedence (highest to lowest):
        </p>
        <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1 ml-2">
          <li>
            Command-line flags (<code>--width 120</code>)
          </li>
          <li>
            Environment variables (
            <code>export ASCII_CHAT_WIDTH=120</code>)
          </li>
          <li>Auto-detection (terminal queries, COLUMNS/ROWS)</li>
          <li>Defaults (110×70, auto color, etc.)</li>
        </ol>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          SSH & Remote Sessions
        </Heading>
        <CodeBlock language="bash">
          {
            "# Set TERM before SSH\nexport TERM=xterm-256color\nssh user@host ascii-chat client\n\n# Or in ~/.ssh/config\nHost *\n  SetEnv TERM=xterm-256color\n\n# Force color mode over SSH\nssh user@host ascii-chat client --color-mode 256"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Optimal Terminal Settings
        </Heading>
        <div className="space-y-2">
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-2"
            >
              Modern Terminal
            </Heading>
            <p className="text-gray-300 text-sm">
              <code>--color-mode truecolor --utf8 true</code>
            </p>
          </div>

          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-2"
            >
              Slow Network / SSH
            </Heading>
            <p className="text-gray-300 text-sm">
              <code>--color-mode 16</code> or{" "}
              <code>--color-mode 256</code>
            </p>
          </div>

          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-2"
            >
              Lightweight / Scripting
            </Heading>
            <p className="text-gray-300 text-sm">
              <code>--color false -x 80 -y 24</code>
            </p>
          </div>
        </div>
        <p className="text-gray-400 text-sm mt-3">
          See also:{" "}
          <TrackedLink
            to="/docs/display"
            label="display common setups"
            className="link-standard"
          >
            Display common setups
          </TrackedLink>{" "}
          for render mode, palette, and FPS combinations.
        </p>
      </div>
    </section>
  );
}
