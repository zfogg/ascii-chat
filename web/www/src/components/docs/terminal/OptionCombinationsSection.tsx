import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function OptionCombinationsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-blue-400">
        🔗 Option Combinations
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-blue-300 mb-3">
          Terminal & Display Dependencies
        </Heading>
        <div className="space-y-2">
          <div className="card-standard accent-blue">
            <Heading level={4} className="text-blue-300 font-semibold mb-2">
              half-block Requires UTF-8
            </Heading>
            <p className="text-gray-300 text-sm">
              <strong>Combination:</strong>{" "}
              <code>--render-mode half-block</code> + <code>--utf8 true</code>
            </p>
          </div>

          <div className="card-standard accent-blue">
            <Heading level={4} className="text-blue-300 font-semibold mb-2">
              Piping Affects Color & Dimensions
            </Heading>
            <p className="text-gray-300 text-sm mb-2">
              When piped, always explicitly set:
            </p>
            <CodeBlock language="bash">
              {`# For piped output, set all three:
ascii-chat mirror -x 120 -y 40 --color true | tee output.txt`}
            </CodeBlock>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Common Setups
        </Heading>
        <div className="space-y-2">
          <div className="card-standard accent-cyan">
            <Heading level={4} className="text-cyan-300 font-semibold mb-2">
              Over SSH
            </Heading>
            <CodeBlock language="bash">
              {`ascii-chat client example.com \\
  --color-mode 256`}
            </CodeBlock>
          </div>

          <div className="card-standard accent-cyan">
            <Heading level={4} className="text-cyan-300 font-semibold mb-2">
              Capture to File
            </Heading>
            <CodeBlock language="bash">
              {`ascii-chat mirror -x 120 -y 40 \\
  --strip-ansi > frame.txt`}
            </CodeBlock>
          </div>

          <div className="card-standard accent-cyan">
            <Heading level={4} className="text-cyan-300 font-semibold mb-2">
              Piped Output
            </Heading>
            <CodeBlock language="bash">
              {`COLUMNS=150 ROWS=50 ascii-chat mirror \\
  --color true | tee output.txt`}
            </CodeBlock>
          </div>
        </div>
      </div>
    </section>
  );
}
