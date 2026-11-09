import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import { MirrorDemoWidget } from "../../demo";
import type { DemoOption } from "../../demo";
import { RenderMode, TextRenderer } from "@ascii-chat/shared/wasm";

const RENDER_MODE_OPTIONS: DemoOption[] = [
  {
    id: "foreground",
    label: "Foreground",
    description: "--render-mode foreground",
    settings: { renderMode: RenderMode.Foreground },
  },
  {
    id: "background",
    label: "Background",
    description: "--render-mode background",
    settings: { renderMode: RenderMode.Background },
  },
  {
    id: "halfblock",
    label: "Half-block",
    description: "--render-mode half-block",
    settings: { renderMode: RenderMode.HalfBlock },
  },
];

const RENDERER_OPTIONS: DemoOption[] = [
  {
    id: "glyph",
    label: "Glyph (default)",
    description: "--renderer glyph",
    settings: { renderer: TextRenderer.Glyph },
  },
  {
    id: "luminance",
    label: "Luminance",
    description: "--renderer luminance",
    settings: { renderer: TextRenderer.Luminance },
  },
];

const STABILIZE_OPTIONS: DemoOption[] = [
  {
    id: "stabilize-on",
    label: "On (default)",
    description: "--stabilize",
    settings: { stabilize: true },
  },
  {
    id: "stabilize-off",
    label: "Off",
    description: "--stabilize=false",
    settings: { stabilize: false },
  },
];

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
      <div className="mt-8">
        <MirrorDemoWidget demoOptions={RENDER_MODE_OPTIONS} />
      </div>
      <div className="docs-subsection-spacing mt-10">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Character Renderer
        </Heading>
        <p className="docs-paragraph">
          Compare glyph shape matching with brightness-to-palette rendering.
          Glyph is the default in the CLI and browser renderer.
        </p>
        <CodeBlock language="bash">
          {
            "# Glyph matching (default)\nascii-chat mirror --renderer glyph\n\n# Brightness-to-palette rendering\nascii-chat mirror --renderer luminance"
          }
        </CodeBlock>
        <MirrorDemoWidget demoOptions={RENDERER_OPTIONS} />
      </div>
      <div className="docs-subsection-spacing mt-10">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Temporal Stabilization
        </Heading>
        <p className="docs-paragraph">
          Stabilization reduces character and color flicker between frames and
          is enabled by default. Turn it off to compare raw frame output.
        </p>
        <CodeBlock language="bash">
          {
            "# Stabilization (default)\nascii-chat mirror --stabilize\n\n# Disable stabilization\nascii-chat mirror --stabilize=false"
          }
        </CodeBlock>
        <MirrorDemoWidget demoOptions={STABILIZE_OPTIONS} />
      </div>
    </section>
  );
}
