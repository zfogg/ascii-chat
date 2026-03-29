import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";
import { MirrorDemoWidget } from "../../demo";
import type { DemoOption } from "../../demo";
import { ColorMode } from "@ascii-chat/shared/wasm";

const COLOR_MODE_OPTIONS: DemoOption[] = [
  {
    id: "truecolor",
    label: "Truecolor",
    description: "--color-mode truecolor",
    settings: { colorMode: ColorMode.TRUECOLOR },
  },
  {
    id: "256",
    label: "256-color",
    description: "--color-mode 256",
    settings: { colorMode: ColorMode.COLOR_256 },
  },
  {
    id: "16",
    label: "16-color",
    description: "--color-mode 16",
    settings: { colorMode: ColorMode.COLOR_16 },
  },
  {
    id: "none",
    label: "Monochrome",
    description: "--color-mode none",
    settings: { colorMode: ColorMode.NONE },
  },
];

export default function ColorControlSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🎨 Color Control
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Enable/Disable Color
        </Heading>
        <p className="docs-paragraph">
          The <code>--color</code> flag controls whether colors are displayed at
          all (independent of color mode):
        </p>
        <CodeBlock language="bash">
          {
            "# Auto-detect (DEFAULT)\nascii-chat client example.com --color auto\n\n# Force colors on (even when piped)\nascii-chat mirror --color true | tee output.txt\n\n# Force colors off (monochrome)\nascii-chat client --color false\n\n# Via environment variable\nASCII_CHAT_COLOR=false ascii-chat client"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>When colors are disabled:</strong> Display switches to
            monochrome (black and white). Piping automatically disables colors
            unless <code>--color true</code> is used.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Color Depth Modes
        </Heading>
        <p className="docs-paragraph">
          Control how many colors are available (when colors are enabled).
          Default is <code>auto</code>, which detects your terminal's
          capability. (Save these in your{" "}
          <TrackedLink
            to="/docs/configuration"
            label="color depth config"
            className="text-cyan-400 hover:text-cyan-300 transition-colors"
          >
            config file
          </TrackedLink>
          )
        </p>
        <CodeBlock language="bash">
          {
            "# Auto-detect (RECOMMENDED)\nascii-chat client --color-mode auto\n\n# Force specific color mode\nascii-chat client --color-mode truecolor  # 16M colors (best quality)\nascii-chat client --color-mode 256        # 256-color xterm\nascii-chat client --color-mode 16         # Basic ANSI colors\nascii-chat client --color-mode none       # Monochrome\n\n# Via environment\nexport ASCII_CHAT_COLOR_MODE=256"
          }
        </CodeBlock>
        <div className="space-y-2 mt-3">
          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Color Mode Details
            </Heading>
            <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
              <li>
                <strong>auto:</strong> Detects via TERM, terminfo, or queries.
                May fail over SSH.
              </li>
              <li>
                <strong>none:</strong> Disables all colors (same as{" "}
                <code>--color false</code>)
              </li>
              <li>
                <strong>16:</strong> Basic 16 ANSI colors (slow connections)
              </li>
              <li>
                <strong>256:</strong> xterm 256-color palette (most terminals)
              </li>
              <li>
                <strong>truecolor:</strong> Full 24-bit RGB color (modern
                terminals only)
              </li>
            </ul>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Logging Color Scheme
        </Heading>
        <p className="docs-paragraph">
          Control the color scheme for ascii-chat's debug/log output (not video
          display):
        </p>
        <CodeBlock language="bash">
          {
            "# Available schemes: pastel, nord, solarized-dark, dracula, gruvbox-dark, monokai, etc.\nascii-chat client --color-scheme dracula\n\n# Via environment\nexport ASCII_CHAT_COLOR_SCHEME=nord"
          }
        </CodeBlock>
      </div>
      <div className="mt-8">
        <MirrorDemoWidget demoOptions={COLOR_MODE_OPTIONS} />
      </div>
    </section>
  );
}
