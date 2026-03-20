import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function VideoTransformsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-indigo-400">
        🔄 Video Transforms
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-indigo-300 mb-3">
          Flip & Mirror
        </Heading>
        <p className="docs-paragraph">
          Flip the video horizontally or vertically. Works with webcam, files,
          and streams.
        </p>
        <CodeBlock language="bash">
          {
            "# Flip horizontally (mirror image)\nascii-chat mirror --flip-x\n\n# Flip vertically (upside down)\nascii-chat mirror --flip-y\n\n# Both flips (180° rotation)\nascii-chat mirror --flip-x --flip-y\n\n# Via environment\nexport ASCII_CHAT_FLIP_X=true"
          }
        </CodeBlock>
        <div className="info-box-note mt-3">
          <p className="text-gray-300 text-sm">
            <strong>macOS Note:</strong> <code>--flip-x</code> defaults to{" "}
            <code>true</code> on Apple devices (mirrored, like FaceTime). Set{" "}
            <code>--flip-x=false</code> to disable.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-blue-300 mb-3">
          Aspect Ratio
        </Heading>
        <p className="docs-paragraph">
          By default, ascii-chat preserves aspect ratio (terminal characters are
          ~2:1 height:width). Use <code>--stretch</code> to ignore aspect ratio
          and fill the terminal:
        </p>
        <CodeBlock language="bash">
          {
            "# Preserve aspect ratio (DEFAULT)\nascii-chat mirror video.mp4\n\n# Stretch to fill terminal (may distort)\nascii-chat mirror video.mp4 --stretch\n\n# Via environment\nexport ASCII_CHAT_STRETCH=true"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
