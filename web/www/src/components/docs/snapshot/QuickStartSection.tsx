import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function QuickStartSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        ⚡ Quick Start
      </Heading>
      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Single Frame Capture
        </Heading>
        <p className="docs-paragraph">
          Capture one frame from webcam and save as ASCII art:
        </p>
        <CodeBlock language="bash">
          {
            "# Capture and display\nascii-chat mirror --snapshot\n\n# Capture and save to file\nascii-chat mirror --snapshot > output.txt"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Timed Capture
        </Heading>
        <p className="docs-paragraph">
          Capture for a specific duration, then exit:
        </p>
        <CodeBlock language="bash">
          {
            "# Capture for 5 seconds\nascii-chat mirror --snapshot --snapshot-delay 5\n\n# Capture for specific duration then exit\nascii-chat client example.com --snapshot --snapshot-delay 3"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
