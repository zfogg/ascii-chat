import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function RenderModesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        📐 Render Modes
      </Heading>

      <div className="docs-subsection-spacing">
        <p className="docs-paragraph">
          Control how ASCII characters are rendered to represent brightness.
          Press <code>r</code> during rendering to cycle through modes.
        </p>
        <CodeBlock language="bash">
          {
            "# Foreground (DEFAULT) - colored text on dark background\nascii-chat client --render-mode foreground\n\n# Background - colored background with white text\nascii-chat client --render-mode background\n\n# Half-block - Unicode blocks for 2x vertical resolution\nascii-chat mirror --render-mode half-block\n\n# Short form\nascii-chat mirror -M half-block"
          }
        </CodeBlock>
        <div className="space-y-2 mt-3">
          <div className="card-standard accent-green">
            <Heading level={4} className="text-green-300 font-semibold mb-2">
              Render Mode Comparison
            </Heading>
            <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
              <li>
                <strong>foreground (fg):</strong> Classic look, good
                compatibility. Colored characters on your terminal background.
              </li>
              <li>
                <strong>background (bg):</strong> Better for light terminals.
                Colored blocks with white text overlay.
              </li>
              <li>
                <strong>half-block:</strong> Double vertical resolution using
                Unicode ▀▄ blocks. Requires UTF-8 (<code>--utf8 true</code>).
              </li>
            </ul>
          </div>
        </div>
      </div>
    </section>
  );
}
