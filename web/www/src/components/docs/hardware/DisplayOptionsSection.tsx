import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function DisplayOptionsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-yellow-400">
        🎨 Display Options
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          Render Modes
        </Heading>
        <p className="docs-paragraph">
          Choose how ASCII characters are rendered (available in client and
          mirror):
        </p>
        <CodeBlock language="bash">
          {
            "# Foreground mode (DEFAULT - colored text on dark background)\nascii-chat client example.com --render-mode foreground\n\n# Background mode (colored background with white text)\nascii-chat client example.com --render-mode background\n\n# Half-block mode (2x vertical resolution using block characters)\nascii-chat mirror --render-mode half-block -M half-block  # Same thing"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Half-Block Mode:</strong> Provides twice the vertical
            resolution by using Unicode block characters. Great for detailed
            images or when you want smaller ASCII art.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          ASCII Palettes
        </Heading>
        <p className="docs-paragraph">
          Control which characters are used to render brightness levels:
        </p>
        <CodeBlock language="bash">
          {
            "# Built-in palettes (all look different)\nascii-chat mirror --palette standard\nascii-chat mirror --palette blocks\nascii-chat mirror --palette digital\nascii-chat mirror --palette minimal\nascii-chat mirror --palette cool\n\n# Custom palette with your own characters\nascii-chat mirror --palette custom --palette-chars '@%#*+=-:. '"
          }
        </CodeBlock>
        <div className="info-box-note mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Custom Palette:</strong> Characters should be ordered from
            darkest to brightest. More characters = more detail.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Frame Rate and Snapshot
        </Heading>
        <CodeBlock language="bash">
          {
            "# Set target frame rate (1-144 FPS, default 60)\nascii-chat client example.com --fps 30\nascii-chat mirror --fps 10  # Lower for slower machines\n\n# Snapshot mode: capture one frame and exit\nascii-chat client example.com --snapshot\nascii-chat mirror --snapshot\n\n# Snapshot with delay (seconds, default 4.0)\nascii-chat client example.com --snapshot --snapshot-delay 10\n\n# Snapshot immediately (no delay)\nascii-chat mirror -S -D 0\n\n# Pipe to clipboard\nascii-chat mirror -S -D 0 | pbcopy  # macOS\nascii-chat mirror -S -D 0 | xclip    # Linux"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Aspect Ratio
        </Heading>
        <CodeBlock language="bash">
          {
            "# Preserve aspect ratio (DEFAULT)\nascii-chat mirror video.mp4\n\n# Allow stretching to fill terminal (may distort image)\nascii-chat mirror --stretch"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
