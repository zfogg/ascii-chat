import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import { MirrorDemoWidget } from "../../demo";
import type { DemoOption } from "../../demo";
import { ColorFilter } from "@ascii-chat/shared/wasm";

const MATRIX_OPTIONS: DemoOption[] = [
  {
    id: "on",
    label: "Matrix Rain",
    description: "--matrix",
    settings: { matrixRain: true, palette: "digital" },
  },
  {
    id: "rainbow",
    label: "Matrix Rainbow",
    description: "--matrix --color-filter rainbow",
    settings: {
      matrixRain: true,
      palette: "digital",
      colorFilter: ColorFilter.RAINBOW,
    },
  },
];

export default function AnimationsEffectsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-emerald-400">
        ✨ Animations & Effects
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-emerald-300 mb-3">
          Matrix Digital Rain
        </Heading>
        <p className="docs-paragraph">
          Enable a Matrix-style digital rain effect that overlays falling
          characters on the video output. Works best with color filters and
          truecolor or 256-color mode.
        </p>
        <CodeBlock language="bash">
          {
            "# Enable Matrix rain effect\nascii-chat mirror --matrix\n\n# Classic green Matrix look\nascii-chat mirror --matrix --color-filter green\n\n# Cyberpunk style\nascii-chat mirror --matrix --color-filter cyan --render-mode background\n\n# With truecolor for best quality\nascii-chat mirror --matrix --color-filter green --color-mode truecolor\n\n# Via environment\nexport ASCII_CHAT_MATRIX=true"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Tip:</strong> The <code>--matrix</code> effect combines well
            with <code>--color-filter green</code> for the classic Matrix look,
            or <code>--color-filter cyan</code> for a cyberpunk aesthetic.
          </p>
        </div>
        <div className="info-box-note mt-3">
          <p className="text-gray-300 text-sm">
            The <code>--matrix</code> effect automatically uses the{" "}
            <code>digital</code> palette, which is a replica of the font used in{" "}
            <em>The Matrix Resurrections</em>. This means the ASCII art rendered
            will have the cool glyphs that rain down as green code from the
            movies. Combine with <code>--color-filter</code> for different color
            schemes.
          </p>
        </div>
      </div>
      <div className="mt-8">
        <MirrorDemoWidget demoOptions={MATRIX_OPTIONS} />
      </div>
    </section>
  );
}
