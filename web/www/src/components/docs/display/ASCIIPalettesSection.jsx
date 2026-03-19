import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ASCIIPalettesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-pink-400">
        🔤 ASCII Palettes
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Built-in Palettes
        </Heading>
        <p className="docs-paragraph">
          Control which ASCII characters represent different brightness
          levels. Each palette has a different aesthetic:
        </p>
        <CodeBlock language="bash">
          {
            "# Built-in palettes\nascii-chat mirror --palette standard   # Default ASCII characters\nascii-chat mirror --palette blocks      # Unicode block elements\nascii-chat mirror --palette digital     # Digital/matrix style\nascii-chat mirror --palette minimal     # Minimal detail\nascii-chat mirror --palette cool        # Alternative aesthetic\n\n# Short form\nascii-chat mirror -P blocks"
          }
        </CodeBlock>
        <div className="space-y-2 mt-3">
          <div className="card-standard accent-pink">
            <Heading
              level={4}
              className="text-pink-300 font-semibold mb-2"
            >
              Palette Characters
            </Heading>
            <div className="space-y-3 text-sm">
              <div>
                <strong className="text-pink-300">standard</strong>
                <span className="text-gray-500 ml-2">(default)</span>
                <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                  <span className="text-gray-600">···</span>
                  {"...',;:clodxkO0KXNWM"}
                </pre>
              </div>
              <div>
                <strong className="text-pink-300">blocks</strong>
                <span className="text-gray-500 ml-2">
                  (requires UTF-8)
                </span>
                <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                  <span className="text-gray-600">···</span>
                  {"░░▒▒▓▓██"}
                </pre>
              </div>
              <div>
                <strong className="text-pink-300">digital</strong>
                <span className="text-gray-500 ml-2">
                  (requires UTF-8)
                </span>
                <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                  <span className="text-gray-600">···</span>
                  {"-=≡≣▰▱◼"}
                </pre>
              </div>
              <div>
                <strong className="text-pink-300">minimal</strong>
                <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                  <span className="text-gray-600">···</span>
                  {".-+*#"}
                </pre>
              </div>
              <div>
                <strong className="text-pink-300">cool</strong>
                <span className="text-gray-500 ml-2">
                  (requires UTF-8)
                </span>
                <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                  <span className="text-gray-600">···</span>
                  {"▁▂▃▄▅▆▇█"}
                </pre>
              </div>
              <div>
                <strong className="text-pink-300">custom</strong>
                <span className="text-gray-500 ml-2">
                  (via <code>--palette-chars</code>)
                </span>
              </div>
            </div>
            <p className="text-gray-500 text-xs mt-3">
              Darkest → brightest.{" "}
              <span className="text-gray-600">···</span> = 3 leading
              spaces (represent black/empty pixels).
            </p>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Custom Palette Characters
        </Heading>
        <p className="docs-paragraph">
          Define your own character gradient from darkest to brightest.
          Supports UTF-8 characters when <code>--utf8 true</code> is set.
        </p>
        <CodeBlock language="bash">
          {
            '# Custom palette (darkest to brightest)\nascii-chat mirror --palette custom --palette-chars " .:-=+*#%@"\n\n# Short form\nascii-chat mirror -P custom -C " .:-=+*#%@"\n\n# UTF-8 custom palette\nascii-chat mirror --utf8 true -P custom -C " ░▒▓█"\n\n# Via environment\nexport ASCII_CHAT_PALETTE=custom\nexport ASCII_CHAT_PALETTE_CHARS=" .:-=+*#%@"'
          }
        </CodeBlock>
      </div>
    </section>
  );
}
