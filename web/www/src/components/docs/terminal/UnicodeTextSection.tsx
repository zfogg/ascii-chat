import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function UnicodeTextSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-indigo-400">
        🌐 Unicode & Text
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-indigo-300 mb-3">
          UTF-8 Support
        </Heading>
        <p className="docs-paragraph">
          Control UTF-8/Unicode character support for palettes and output:
        </p>
        <CodeBlock language="bash">
          {
            "# Auto-detect (DEFAULT) - based on locale\nascii-chat mirror --utf8 auto\n\n# Force UTF-8 on\nascii-chat mirror --utf8 true --palette blocks\n\n# Force UTF-8 off (ASCII-only)\nascii-chat mirror --utf8 false\n\n# Via environment\nexport ASCII_CHAT_UTF8=true"
          }
        </CodeBlock>
        <div className="info-box-warning mt-3">
          <p className="text-gray-300 text-sm">
            <strong>⚠️ Warning:</strong> Some palettes (blocks, half-block)
            require UTF-8. They'll fall back to ASCII if UTF-8 is disabled.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-blue-300 mb-3">
          Strip ANSI Codes
        </Heading>
        <p className="docs-paragraph">
          Remove ANSI escape sequences from output (for saving to files or
          processing text):
        </p>
        <CodeBlock language="bash">
          {
            "# Capture plain text (no colors/formatting)\nascii-chat mirror --snapshot -D 0 --strip-ansi > frame.txt\n\n# Pipe output and remove ANSI codes\nascii-chat mirror | ascii-chat mirror --strip-ansi | tee plain.txt\n\n# Via environment\nexport ASCII_CHAT_STRIP_ANSI=true"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
