import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";

export default function Display() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Display", path: "/docs/display" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Display - ascii-chat Documentation"
        description="Render modes, ASCII palettes, color filters, animations, framerate, and video transforms for ascii-chat."
        url={`${SITES.MAIN}/docs/display`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-fuchsia-400">🎨</span> Display & Rendering
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Render modes, palettes, color filters, animations, framerate, and
              video transforms
            </p>
          </header>

          {/* Important Notes */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-fuchsia-400">
              📌 Important Notes
            </Heading>

            <div className="space-y-3">
              <div className="info-box-info">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🎮 Interactive Controls:</strong> Many display options
                  can be toggled at runtime. Press <code>r</code> to cycle
                  render modes, <code>c</code> to cycle color modes, and{" "}
                  <code>-</code> to toggle the FPS counter.
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🎨 Color Filters:</strong> Using{" "}
                  <code>--color-filter</code> automatically sets{" "}
                  <code>--color-mode</code> to mono. The filter tints grayscale
                  video with a single color.
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🖥️ Terminal Settings:</strong> For color mode, UTF-8,
                  dimensions, and terminal capabilities, see the{" "}
                  <TrackedLink
                    to="/docs/terminal"
                    label="terminal settings"
                    className="text-cyan-400 hover:text-cyan-300 transition-colors"
                  >
                    Terminal
                  </TrackedLink>{" "}
                  page.
                </p>
              </div>
            </div>
          </section>

          {/* Render Modes */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              📐 Render Modes
            </Heading>

            <div className="docs-subsection-spacing">
              <p className="docs-paragraph">
                Control how ASCII characters are rendered to represent
                brightness. Press <code>r</code> during rendering to cycle
                through modes.
              </p>
              <CodeBlock language="bash">
                {
                  "# Foreground (DEFAULT) - colored text on dark background\nascii-chat client --render-mode foreground\n\n# Background - colored background with white text\nascii-chat client --render-mode background\n\n# Half-block - Unicode blocks for 2x vertical resolution\nascii-chat mirror --render-mode half-block\n\n# Short form\nascii-chat mirror -M half-block"
                }
              </CodeBlock>
              <div className="space-y-2 mt-3">
                <div className="card-standard accent-green">
                  <Heading
                    level={4}
                    className="text-green-300 font-semibold mb-2"
                  >
                    Render Mode Comparison
                  </Heading>
                  <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      <strong>foreground (fg):</strong> Classic look, good
                      compatibility. Colored characters on your terminal
                      background.
                    </li>
                    <li>
                      <strong>background (bg):</strong> Better for light
                      terminals. Colored blocks with white text overlay.
                    </li>
                    <li>
                      <strong>half-block:</strong> Double vertical resolution
                      using Unicode ▀▄ blocks. Requires UTF-8 (
                      <code>--utf8 true</code>).
                    </li>
                  </ul>
                </div>
              </div>
            </div>
          </section>

          {/* ASCII Palettes */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-pink-400">
              🔤 ASCII Palettes
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-pink-300 mb-3">
                Built-in Palettes
              </Heading>
              <p className="docs-paragraph">
                Control which ASCII characters represent different brightness
                levels. Each palette has a different aesthetic:
              </p>
              <CodeBlock language="bash">
                {
                  "# Built-in palettes\nascii-chat mirror --palette standard   # Default ASCII characters\nascii-chat mirror --palette blocks      # Unicode block elements\nascii-chat mirror --palette digital     # Digital/matrix style\nascii-chat mirror --palette minimal     # Minimal detail\nascii-chat mirror --palette cool        # Alternative aesthetic\n\n# Short form\nascii-chat mirror -P blocks"
                }
              </CodeBlock>
              <div className="space-y-2 mt-3">
                <div className="card-standard accent-pink">
                  <Heading
                    level={4}
                    className="text-pink-300 font-semibold mb-2"
                  >
                    Palette Characters
                  </Heading>
                  <div className="space-y-3 text-sm">
                    <div>
                      <strong className="text-pink-300">standard</strong>
                      <span className="text-gray-500 ml-2">(default)</span>
                      <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                        <span className="text-gray-600">···</span>
                        {"...',;:clodxkO0KXNWM"}
                      </pre>
                    </div>
                    <div>
                      <strong className="text-pink-300">blocks</strong>
                      <span className="text-gray-500 ml-2">
                        (requires UTF-8)
                      </span>
                      <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                        <span className="text-gray-600">···</span>
                        {"░░▒▒▓▓██"}
                      </pre>
                    </div>
                    <div>
                      <strong className="text-pink-300">digital</strong>
                      <span className="text-gray-500 ml-2">
                        (requires UTF-8)
                      </span>
                      <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                        <span className="text-gray-600">···</span>
                        {"-=≡≣▰▱◼"}
                      </pre>
                    </div>
                    <div>
                      <strong className="text-pink-300">minimal</strong>
                      <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                        <span className="text-gray-600">···</span>
                        {".-+*#"}
                      </pre>
                    </div>
                    <div>
                      <strong className="text-pink-300">cool</strong>
                      <span className="text-gray-500 ml-2">
                        (requires UTF-8)
                      </span>
                      <pre className="mt-1 font-mono text-lg text-gray-200 bg-gray-950 rounded px-3 py-2 overflow-x-auto">
                        <span className="text-gray-600">···</span>
                        {"▁▂▃▄▅▆▇█"}
                      </pre>
                    </div>
                    <div>
                      <strong className="text-pink-300">custom</strong>
                      <span className="text-gray-500 ml-2">
                        (via <code>--palette-chars</code>)
                      </span>
                    </div>
                  </div>
                  <p className="text-gray-500 text-xs mt-3">
                    Darkest → brightest.{" "}
                    <span className="text-gray-600">···</span> = 3 leading
                    spaces (represent black/empty pixels).
                  </p>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-purple-300 mb-3">
                Custom Palette Characters
              </Heading>
              <p className="docs-paragraph">
                Define your own character gradient from darkest to brightest.
                Supports UTF-8 characters when <code>--utf8 true</code> is set.
              </p>
              <CodeBlock language="bash">
                {
                  '# Custom palette (darkest to brightest)\nascii-chat mirror --palette custom --palette-chars " .:-=+*#%@"\n\n# Short form\nascii-chat mirror -P custom -C " .:-=+*#%@"\n\n# UTF-8 custom palette\nascii-chat mirror --utf8 true -P custom -C " ░▒▓█"\n\n# Via environment\nexport ASCII_CHAT_PALETTE=custom\nexport ASCII_CHAT_PALETTE_CHARS=" .:-=+*#%@"'
                }
              </CodeBlock>
            </div>
          </section>

          {/* Color Filters */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-cyan-400">
              🌈 Color Filters
            </Heading>

            <div className="docs-subsection-spacing">
              <p className="docs-paragraph">
                Apply a monochromatic color tint to the video output. The filter
                converts the image to grayscale and applies the chosen color.
                Using a color filter automatically sets the color mode to mono.
              </p>
              <CodeBlock language="bash">
                {
                  "# No filter (DEFAULT)\nascii-chat mirror --color-filter none\n\n# Single color tints\nascii-chat mirror --color-filter green     # Classic terminal green\nascii-chat mirror --color-filter cyan      # Cool blue-green\nascii-chat mirror --color-filter fuchsia   # Vibrant pink\nascii-chat mirror --color-filter orange    # Warm amber\nascii-chat mirror --color-filter red       # Intense red\n\n# Rainbow mode - cycles through colors\nascii-chat mirror --color-filter rainbow\n\n# Via environment\nexport ASCII_CHAT_COLOR_FILTER=green"
                }
              </CodeBlock>
              <div className="space-y-2 mt-3">
                <div className="card-standard accent-cyan">
                  <Heading
                    level={4}
                    className="text-cyan-300 font-semibold mb-2"
                  >
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
          </section>

          {/* Animations & Effects */}
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
                characters on the video output. Works best with color filters
                and truecolor or 256-color mode.
              </p>
              <CodeBlock language="bash">
                {
                  "# Enable Matrix rain effect\nascii-chat mirror --matrix\n\n# Classic green Matrix look\nascii-chat mirror --matrix --color-filter green\n\n# Cyberpunk style\nascii-chat mirror --matrix --color-filter cyan --render-mode background\n\n# With truecolor for best quality\nascii-chat mirror --matrix --color-filter green --color-mode truecolor\n\n# Via environment\nexport ASCII_CHAT_MATRIX=true"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Tip:</strong> The <code>--matrix</code> effect
                  combines well with <code>--color-filter green</code> for the
                  classic Matrix look, or <code>--color-filter cyan</code> for a
                  cyberpunk aesthetic.
                </p>
              </div>
              <div className="info-box-note mt-3">
                <p className="text-gray-300 text-sm">
                  Using <code>--color-filter</code> together with{" "}
                  <code>--matrix</code> automatically switches the built-in font
                  to a replica of the one used to make{" "}
                  <em>The Matrix Resurrections</em>. This means the ASCII art
                  rendered will have the cool glyphs that rain down as green
                  code from the movies.
                </p>
              </div>
            </div>
          </section>

          {/* Framerate */}
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

          {/* Video Transforms */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-indigo-400">
              🔄 Video Transforms
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-indigo-300 mb-3">
                Flip & Mirror
              </Heading>
              <p className="docs-paragraph">
                Flip the video horizontally or vertically. Works with webcam,
                files, and streams.
              </p>
              <CodeBlock language="bash">
                {
                  "# Flip horizontally (mirror image)\nascii-chat mirror --flip-x\n\n# Flip vertically (upside down)\nascii-chat mirror --flip-y\n\n# Both flips (180° rotation)\nascii-chat mirror --flip-x --flip-y\n\n# Via environment\nexport ASCII_CHAT_FLIP_X=true"
                }
              </CodeBlock>
              <div className="info-box-note mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>macOS Note:</strong> <code>--flip-x</code> defaults to{" "}
                  <code>true</code> on Apple devices (mirrored, like FaceTime).
                  Set <code>--flip-x=false</code> to disable.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-blue-300 mb-3">
                Aspect Ratio
              </Heading>
              <p className="docs-paragraph">
                By default, ascii-chat preserves aspect ratio (terminal
                characters are ~2:1 height:width). Use <code>--stretch</code> to
                ignore aspect ratio and fill the terminal:
              </p>
              <CodeBlock language="bash">
                {
                  "# Preserve aspect ratio (DEFAULT)\nascii-chat mirror video.mp4\n\n# Stretch to fill terminal (may distort)\nascii-chat mirror video.mp4 --stretch\n\n# Via environment\nexport ASCII_CHAT_STRETCH=true"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Quick Reference */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-purple-400">
              📋 Quick Reference
            </Heading>

            <div className="table-wrapper">
              <table className="table-base">
                <thead className="table-header">
                  <tr>
                    <th className="table-header-cell">Option</th>
                    <th className="table-header-cell">Short</th>
                    <th className="table-header-cell">Default</th>
                    <th className="table-header-cell">Description</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td className="table-body-cell">
                      <code>--render-mode</code>
                    </td>
                    <td className="table-body-cell">
                      <code>-M</code>
                    </td>
                    <td className="table-body-cell">foreground</td>
                    <td className="table-body-cell">
                      Render mode (foreground, background, half-block)
                    </td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--palette</code>
                    </td>
                    <td className="table-body-cell">
                      <code>-P</code>
                    </td>
                    <td className="table-body-cell">standard</td>
                    <td className="table-body-cell">ASCII palette preset</td>
                  </tr>
                  <tr>
                    <td className="table-body-cell">
                      <code>--palette-chars</code>
                    </td>
                    <td className="table-body-cell">
                      <code>-C</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">
                      Custom characters (darkest→brightest)
                    </td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--color-filter</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">none</td>
                    <td className="table-body-cell">
                      Monochromatic color tint
                    </td>
                  </tr>
                  <tr>
                    <td className="table-body-cell">
                      <code>--matrix</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">
                      Matrix digital rain effect
                    </td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--fps</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">60</td>
                    <td className="table-body-cell">
                      Target framerate (1-144)
                    </td>
                  </tr>
                  <tr>
                    <td className="table-body-cell">
                      <code>--fps-counter</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">
                      Show FPS overlay (toggle: <code>-</code>)
                    </td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--flip-x</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">Flip horizontally</td>
                  </tr>
                  <tr>
                    <td className="table-body-cell">
                      <code>--flip-y</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">Flip vertically</td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--stretch</code>
                    </td>
                    <td className="table-body-cell">—</td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">
                      Ignore aspect ratio, fill terminal
                    </td>
                  </tr>
                  <tr>
                    <td className="table-body-cell">
                      <code>--snapshot</code>
                    </td>
                    <td className="table-body-cell">
                      <code>-S</code>
                    </td>
                    <td className="table-body-cell">false</td>
                    <td className="table-body-cell">
                      Capture one frame and exit
                    </td>
                  </tr>
                  <tr className="table-row-alt">
                    <td className="table-body-cell">
                      <code>--snapshot-delay</code>
                    </td>
                    <td className="table-body-cell">
                      <code>-D</code>
                    </td>
                    <td className="table-body-cell">0</td>
                    <td className="table-body-cell">
                      Delay before snapshot (seconds)
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </section>

          <Footer />
        </div>
      </div>
    </>
  );
}
