import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../../../TrackedLink";

export default function WebcamSetupSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🎬 Webcam Setup
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          List Available Webcams
        </Heading>
        <p className="docs-paragraph">
          See all connected webcam devices and their indices (works in client
          and mirror modes):
        </p>
        <CodeBlock language="bash">ascii-chat --list-webcams</CodeBlock>
        <p className="text-gray-400 text-sm mt-2">
          Shows device index, name, and platform-specific details
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Select Specific Webcam
        </Heading>
        <p className="docs-paragraph">
          Use <code className="text-cyan-300">--webcam-index</code> (or short
          form <code className="text-cyan-300">-c</code>) to choose a specific
          camera. (Save it in your{" "}
          <TrackedLink
            to="/docs/configuration"
            label="webcam-index config"
            className="text-cyan-400 hover:text-cyan-300 transition-colors"
          >
            config file
          </TrackedLink>
          )
        </p>
        <CodeBlock language="bash">
          {
            "# Use first webcam (default: index 0)\nascii-chat client example.com\n\n# Use second webcam\nascii-chat client example.com --webcam-index 1\n\n# Short form\nascii-chat client example.com -c 2"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Flip Webcam Video
        </Heading>
        <p className="docs-paragraph">
          Control horizontal and vertical mirroring of webcam output with{" "}
          <code className="text-cyan-300">--flip-x</code> and{" "}
          <code className="text-cyan-300">--flip-y</code> flags.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Test Pattern Mode
        </Heading>
        <p className="docs-paragraph">
          Use a test pattern instead of actual webcam (useful for testing,
          CI/CD, or when no camera is available):
        </p>
        <CodeBlock language="bash">
          {
            "# Test pattern mode\nascii-chat client example.com --test-pattern\nascii-chat mirror --test-pattern\n\n# Via environment variable\nWEBCAM_DISABLED=1 ascii-chat client example.com"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
