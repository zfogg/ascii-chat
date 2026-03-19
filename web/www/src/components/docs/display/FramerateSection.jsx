import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function FramerateSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-yellow-400">
        ⚡ Framerate
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          Target FPS
        </Heading>
        <p className="docs-paragraph">
          Control the target framerate for rendering. Higher values give
          smoother output but use more CPU and bandwidth. Default is 60
          FPS.
        </p>
        <CodeBlock language="bash">
          {
            "# Default (60 FPS)\nascii-chat client example.com\n\n# High framerate for high refresh-rate terminals\nascii-chat mirror --fps 144\n\n# Low framerate for slow connections\nascii-chat client example.com --fps 15\n\n# Via environment\nexport ASCII_CHAT_FPS=60"
          }
        </CodeBlock>
        <div className="card-standard accent-yellow mt-3">
          <Heading
            level={4}
            className="text-yellow-300 font-semibold mb-2"
          >
            FPS Guidelines
          </Heading>
          <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
            <li>
              <strong>1-15:</strong> Low bandwidth / slow connections
            </li>
            <li>
              <strong>30:</strong> Good balance of quality and performance
            </li>
            <li>
              <strong>60:</strong> Default, smooth output for modern
              terminals
            </li>
            <li>
              <strong>144:</strong> Maximum, for high refresh-rate
              terminals
            </li>
          </ul>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          FPS Counter Overlay
        </Heading>
        <p className="docs-paragraph">
          Show a live FPS counter in the top-right corner of the display.
          Press <code>-</code> during rendering to toggle.
        </p>
        <CodeBlock language="bash">
          {
            "# Enable FPS counter\nascii-chat mirror --fps-counter\n\n# Combine with target FPS\nascii-chat mirror --fps 60 --fps-counter\n\n# Via environment\nexport ASCII_CHAT_FPS_COUNTER=true"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
