import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import { MirrorDemoWidget } from "../../demo";
import type { DemoOption } from "../../demo";
import { ColorFilter } from "@ascii-chat/shared/wasm";

const COLOR_FILTER_OPTIONS: DemoOption[] = [
  {
    id: "rainbow",
    label: "Rainbow",
    settings: { colorFilter: ColorFilter.RAINBOW },
  },
  { id: "green", label: "Green", settings: { colorFilter: ColorFilter.GREEN } },
  { id: "cyan", label: "Cyan", settings: { colorFilter: ColorFilter.CYAN } },
  {
    id: "magenta",
    label: "Magenta",
    settings: { colorFilter: ColorFilter.MAGENTA },
  },
  {
    id: "orange",
    label: "Orange",
    settings: { colorFilter: ColorFilter.ORANGE },
  },
  {
    id: "yellow",
    label: "Yellow",
    settings: { colorFilter: ColorFilter.YELLOW },
  },
  { id: "pink", label: "Pink", settings: { colorFilter: ColorFilter.PINK } },
  { id: "red", label: "Red", settings: { colorFilter: ColorFilter.RED } },
  { id: "teal", label: "Teal", settings: { colorFilter: ColorFilter.TEAL } },
];

export default function ColorFiltersSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        🌈 Color Filters
      </Heading>

      <div className="docs-subsection-spacing">
        <p className="docs-paragraph">
          Apply a monochromatic color tint to the video output. The filter
          converts the image to grayscale and applies the chosen color. Using a
          color filter automatically sets the color mode to mono.
        </p>
        <CodeBlock language="bash">
          {
            "# No filter (DEFAULT)\nascii-chat mirror --color-filter none\n\n# Single color tints\nascii-chat mirror --color-filter green     # Classic terminal green\nascii-chat mirror --color-filter cyan      # Cool blue-green\nascii-chat mirror --color-filter fuchsia   # Vibrant pink\nascii-chat mirror --color-filter orange    # Warm amber\nascii-chat mirror --color-filter red       # Intense red\n\n# Rainbow mode - cycles through colors\nascii-chat mirror --color-filter rainbow\n\n# Via environment\nexport ASCII_CHAT_COLOR_FILTER=green"
          }
        </CodeBlock>
        <div className="space-y-2 mt-3">
          <div className="card-standard accent-cyan">
            <Heading level={4} className="text-cyan-300 font-semibold mb-2">
              Available Filters
            </Heading>
            <div className="grid grid-cols-2 sm:grid-cols-3 gap-2 text-sm">
              <div>
                <code>none</code>
                {" — "}
                <span className="text-gray-500">no filter</span>
              </div>
              <div>
                <code>black</code>
                {" — "}
                <span style={{ color: "#666666" }}>dark tint</span>
              </div>
              <div>
                <code>white</code>
                {" — "}
                <span style={{ color: "#FFFFFF" }}>light tint</span>
              </div>
              <div>
                <code>green</code>
                {" — "}
                <span style={{ color: "#00FF41" }}>terminal green</span>
              </div>
              <div>
                <code>magenta</code>
                {" — "}
                <span style={{ color: "#FF00FF" }}>magenta</span>
              </div>
              <div>
                <code>fuchsia</code>
                {" — "}
                <span style={{ color: "#FF00AA" }}>fuchsia</span>
              </div>
              <div>
                <code>orange</code>
                {" — "}
                <span style={{ color: "#FF8800" }}>warm amber</span>
              </div>
              <div>
                <code>teal</code>
                {" — "}
                <span style={{ color: "#00DDDD" }}>blue-green</span>
              </div>
              <div>
                <code>cyan</code>
                {" — "}
                <span style={{ color: "#00FFFF" }}>cool cyan</span>
              </div>
              <div>
                <code>pink</code>
                {" — "}
                <span style={{ color: "#FFB6C1" }}>soft pink</span>
              </div>
              <div>
                <code>red</code>
                {" — "}
                <span style={{ color: "#FF3333" }}>intense red</span>
              </div>
              <div>
                <code>yellow</code>
                {" — "}
                <span style={{ color: "#FFEB99" }}>warm yellow</span>
              </div>
              <div>
                <code>rainbow</code>
                {" — "}
                <span
                  style={{
                    backgroundImage:
                      "linear-gradient(to right, #FF3333, #FF8800, #FFEB99, #00FF41, #00FFFF, #FF00FF)",
                    WebkitBackgroundClip: "text",
                    WebkitTextFillColor: "transparent",
                  }}
                >
                  cycling spectrum
                </span>
              </div>
            </div>
          </div>
        </div>
      </div>
      <div className="mt-8">
        <MirrorDemoWidget demoOptions={COLOR_FILTER_OPTIONS} />
      </div>
    </section>
  );
}
