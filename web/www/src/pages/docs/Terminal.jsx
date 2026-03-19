import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";

export default function Terminal() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Terminal", path: "/docs/terminal" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Terminal - ascii-chat Documentation"
        description="Terminal color modes, dimensions, UTF-8 support, and terminal capabilities for ascii-chat."
        url={`${SITES.MAIN}/docs/terminal`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-cyan-400">🖥️</span> Terminal Rendering
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Colors, dimensions, Unicode support, and terminal capabilities
            </p>
          </header>

          {/* Important Notes */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-cyan-400">
              📌 Important Notes
            </Heading>

            <div className="space-y-3">
              <div className="info-box-info">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🔍 Auto-Detection:</strong> ascii-chat automatically
                  detects terminal capabilities at startup using{" "}
                  <code>isatty()</code>. When output is piped or redirected,
                  detection behaves differently.
                </p>
              </div>

              <div className="info-box-info">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>📐 Default Dimensions:</strong> Width 110, Height 70
                  characters. Auto-detection via <code>$COLUMNS</code> and{" "}
                  <code>$ROWS</code> environment variables (or terminal
                  queries). Override with <code>-x</code> and <code>-y</code>.
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🌐 UTF-8 & Unicode:</strong> Automatically detected by
                  locale. Use <code>--utf8</code> to override. Some render modes
                  (half-block, blocks) require UTF-8 support.
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>📦 Piping & Redirection:</strong> When output is piped
                  (e.g., <code>ascii-chat mirror | tee file.txt</code>
                  ), color and terminal detection change. Use{" "}
                  <code>--strip-ansi</code> to remove ANSI codes from piped
                  output.
                </p>
              </div>
            </div>
          </section>

          {/* Color Control */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-purple-400">
              🎨 Color Control
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-purple-300 mb-3">
                Enable/Disable Color
              </Heading>
              <p className="docs-paragraph">
                The <code>--color</code> flag controls whether colors are
                displayed at all (independent of color mode):
              </p>
              <CodeBlock language="bash">
                {
                  "# Auto-detect (DEFAULT)\nascii-chat client example.com --color auto\n\n# Force colors on (even when piped)\nascii-chat mirror --color true | tee output.txt\n\n# Force colors off (monochrome)\nascii-chat client --color false\n\n# Via environment variable\nASCII_CHAT_COLOR=false ascii-chat client"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>When colors are disabled:</strong> Display switches to
                  monochrome (black and white). Piping automatically disables
                  colors unless <code>--color true</code> is used.
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
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Color Mode Details
                  </Heading>
                  <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      <strong>auto:</strong> Detects via TERM, terminfo, or
                      queries. May fail over SSH.
                    </li>
                    <li>
                      <strong>none:</strong> Disables all colors (same as{" "}
                      <code>--color false</code>)
                    </li>
                    <li>
                      <strong>16:</strong> Basic 16 ANSI colors (slow
                      connections)
                    </li>
                    <li>
                      <strong>256:</strong> xterm 256-color palette (most
                      terminals)
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
                Control the color scheme for ascii-chat's debug/log output (not
                video display):
              </p>
              <CodeBlock language="bash">
                {
                  "# Available schemes: pastel, nord, solarized-dark, dracula, gruvbox-dark, monokai, etc.\nascii-chat client --color-scheme dracula\n\n# Via environment\nexport ASCII_CHAT_COLOR_SCHEME=nord"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Display & Rendering */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              🎨 Display & Rendering
            </Heading>

            <p className="docs-paragraph">
              For render modes, ASCII palettes, color filters, animations,
              framerate, and video transforms, see the{" "}
              <TrackedLink
                to="/docs/display"
                label="display docs"
                className="link-standard"
              >
                Display page
              </TrackedLink>
              .
            </p>
          </section>

          {/* Terminal Dimensions */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-teal-400">
              📏 Terminal Dimensions
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-teal-300 mb-3">
                Auto-Detection & Environment Variables
              </Heading>
              <p className="docs-paragraph">
                Terminal dimensions are automatically detected via:
              </p>
              <ul className="list-disc list-inside text-gray-300 text-sm space-y-1 ml-2">
                <li>
                  Terminal queries (ioctl TIOCGWINSZ on Unix, Console API on
                  Windows)
                </li>
                <li>
                  <code>$COLUMNS</code> and <code>$ROWS</code> environment
                  variables (when set)
                </li>
                <li>Fallback defaults: 110×70 if detection fails</li>
              </ul>
              <CodeBlock language="bash">
                {
                  "# Set dimensions via environment variables\nexport COLUMNS=120 ROWS=40\nascii-chat client example.com\n\n# Or set just for this command\nCOLUMNS=150 ROWS=50 ascii-chat mirror video.mp4"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Auto-Update:</strong> When running interactively (not
                  piped), dimensions are updated in real-time when you resize
                  the terminal.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-cyan-300 mb-3">
                Custom Dimensions
              </Heading>
              <p className="docs-paragraph">
                Override auto-detection with explicit width and height:
              </p>
              <CodeBlock language="bash">
                {
                  "# Set custom dimensions\nascii-chat client --width 120 --height 40\n\n# Short form\nascii-chat client -x 120 -y 40\n\n# Very wide for multiple columns\nascii-chat mirror -x 200 -y 60\n\n# Minimal size\nascii-chat client -x 20 -y 10\n\n# Via environment variables\nexport ASCII_CHAT_WIDTH=150 ASCII_CHAT_HEIGHT=50"
                }
              </CodeBlock>
              <div className="info-box-note mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Valid Range:</strong> width 20-512, height 10-256
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-purple-300 mb-3">
                Piping & Non-Interactive Mode
              </Heading>
              <p className="docs-paragraph">
                When output is piped (
                <code>ascii-chat mirror | tee file.txt</code>
                ), dimension detection changes:
              </p>
              <ul className="list-disc list-inside text-gray-300 text-sm space-y-1 ml-2">
                <li>
                  Terminal queries fail (<code>isatty()</code> returns false)
                </li>
                <li>
                  Falls back to <code>$COLUMNS</code> and <code>$ROWS</code>
                </li>
                <li>Uses defaults (110×70) if env vars not set</li>
                <li>
                  Always use <code>-x</code>/<code>-y</code> for consistent
                  output
                </li>
              </ul>
              <CodeBlock language="bash">
                {
                  "# Capture single frame to file with explicit size\nascii-chat mirror -x 120 -y 40 --snapshot -D 0 > frame.txt\n\n# Pipe to file with consistent dimensions\nascii-chat mirror -x 100 -y 30 | tee output.txt\n\n# Use with COLUMNS/ROWS for piped output\nCOLUMNS=200 ROWS=60 ascii-chat mirror | cat"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Unicode & Text */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-indigo-400">
              🌐 Unicode & Text
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-indigo-300 mb-3">
                UTF-8 Support
              </Heading>
              <p className="docs-paragraph">
                Control UTF-8/Unicode character support for palettes and output:
              </p>
              <CodeBlock language="bash">
                {
                  "# Auto-detect (DEFAULT) - based on locale\nascii-chat mirror --utf8 auto\n\n# Force UTF-8 on\nascii-chat mirror --utf8 true --palette blocks\n\n# Force UTF-8 off (ASCII-only)\nascii-chat mirror --utf8 false\n\n# Via environment\nexport ASCII_CHAT_UTF8=true"
                }
              </CodeBlock>
              <div className="info-box-warning mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>⚠️ Warning:</strong> Some palettes (blocks,
                  half-block) require UTF-8. They'll fall back to ASCII if UTF-8
                  is disabled.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-blue-300 mb-3">
                Strip ANSI Codes
              </Heading>
              <p className="docs-paragraph">
                Remove ANSI escape sequences from output (for saving to files or
                processing text):
              </p>
              <CodeBlock language="bash">
                {
                  "# Capture plain text (no colors/formatting)\nascii-chat mirror --snapshot -D 0 --strip-ansi > frame.txt\n\n# Pipe output and remove ANSI codes\nascii-chat mirror | ascii-chat mirror --strip-ansi | tee plain.txt\n\n# Via environment\nexport ASCII_CHAT_STRIP_ANSI=true"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Snapshot Mode */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-orange-400">
              📸 Snapshot Mode
            </Heading>

            <p className="docs-paragraph">
              For comprehensive snapshot capture documentation, see the{" "}
              <a href="/docs/snapshot" className="link-standard">
                Snapshot page
              </a>
              .
            </p>
          </section>

          {/* Terminal Detection & Debugging */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-pink-400">
              🔍 Debugging & Detection
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-pink-300 mb-3">
                Show Terminal Capabilities
              </Heading>
              <p className="docs-paragraph">
                Display detected terminal capabilities and configuration:
              </p>
              <CodeBlock language="bash">
                ascii-chat --show-capabilities
              </CodeBlock>
              <p className="text-gray-400 text-sm mt-2">
                Output includes: color support, Unicode support, terminal size,
                isatty status, environment variables, and detected TERM type.
              </p>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-purple-300 mb-3">
                Debug Display Issues
              </Heading>
              <div className="space-y-3">
                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Colors Look Wrong
                  </Heading>
                  <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      Run <code>--show-capabilities</code>
                    </li>
                    <li>
                      Check TERM: <code>echo $TERM</code>
                    </li>
                    <li>
                      Force a color mode: <code>--color-mode 256</code>
                    </li>
                    <li>
                      Over SSH, set <code>export TERM=xterm-256color</code>
                    </li>
                  </ol>
                </div>

                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Unicode Characters Broken
                  </Heading>
                  <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      Check locale: <code>locale</code>
                    </li>
                    <li>
                      Ensure UTF-8 locale: <code>export LANG=en_US.UTF-8</code>
                    </li>
                    <li>
                      Force UTF-8: <code>--utf8 true</code>
                    </li>
                  </ol>
                </div>

                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Dimensions Wrong
                  </Heading>
                  <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      Check detected size: <code>--show-capabilities</code>
                    </li>
                    <li>
                      Try explicit size: <code>-x 120 -y 40</code>
                    </li>
                    <li>
                      When piped, set: <code>COLUMNS=120 ROWS=40</code>
                    </li>
                  </ol>
                </div>
              </div>
            </div>
          </section>

          {/* Option Combinations */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-blue-400">
              🔗 Option Combinations
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-blue-300 mb-3">
                Terminal & Display Dependencies
              </Heading>
              <div className="space-y-2">
                <div className="card-standard accent-blue">
                  <Heading
                    level={4}
                    className="text-blue-300 font-semibold mb-2"
                  >
                    half-block Requires UTF-8
                  </Heading>
                  <p className="text-gray-300 text-sm">
                    <strong>Combination:</strong>{" "}
                    <code>--render-mode half-block</code> +{" "}
                    <code>--utf8 true</code>
                  </p>
                </div>

                <div className="card-standard accent-blue">
                  <Heading
                    level={4}
                    className="text-blue-300 font-semibold mb-2"
                  >
                    Piping Affects Color & Dimensions
                  </Heading>
                  <p className="text-gray-300 text-sm mb-2">
                    When piped, always explicitly set:
                  </p>
                  <CodeBlock language="bash">
                    {`# For piped output, set all three:
ascii-chat mirror -x 120 -y 40 --color true | tee output.txt`}
                  </CodeBlock>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-cyan-300 mb-3">
                Common Setups
              </Heading>
              <div className="space-y-2">
                <div className="card-standard accent-cyan">
                  <Heading
                    level={4}
                    className="text-cyan-300 font-semibold mb-2"
                  >
                    Over SSH
                  </Heading>
                  <CodeBlock language="bash">
                    {`ascii-chat client example.com \\
  --color-mode 256`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <Heading
                    level={4}
                    className="text-cyan-300 font-semibold mb-2"
                  >
                    Capture to File
                  </Heading>
                  <CodeBlock language="bash">
                    {`ascii-chat mirror -x 120 -y 40 \\
  --strip-ansi > frame.txt`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <Heading
                    level={4}
                    className="text-cyan-300 font-semibold mb-2"
                  >
                    Piped Output
                  </Heading>
                  <CodeBlock language="bash">
                    {`COLUMNS=150 ROWS=50 ascii-chat mirror \\
  --color true | tee output.txt`}
                  </CodeBlock>
                </div>
              </div>
            </div>
          </section>

          {/* Tips & Tricks */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              💡 Tips & Tricks
            </Heading>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-green-300 mb-3">
                Environment Variable Precedence
              </Heading>
              <p className="docs-paragraph">
                Order of precedence (highest to lowest):
              </p>
              <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1 ml-2">
                <li>
                  Command-line flags (<code>--width 120</code>)
                </li>
                <li>
                  Environment variables (
                  <code>export ASCII_CHAT_WIDTH=120</code>)
                </li>
                <li>Auto-detection (terminal queries, COLUMNS/ROWS)</li>
                <li>Defaults (110×70, auto color, etc.)</li>
              </ol>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-cyan-300 mb-3">
                SSH & Remote Sessions
              </Heading>
              <CodeBlock language="bash">
                {
                  "# Set TERM before SSH\nexport TERM=xterm-256color\nssh user@host ascii-chat client\n\n# Or in ~/.ssh/config\nHost *\n  SetEnv TERM=xterm-256color\n\n# Force color mode over SSH\nssh user@host ascii-chat client --color-mode 256"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <Heading level={3} className="heading-3 text-purple-300 mb-3">
                Optimal Terminal Settings
              </Heading>
              <div className="space-y-2">
                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Modern Terminal
                  </Heading>
                  <p className="text-gray-300 text-sm">
                    <code>--color-mode truecolor --utf8 true</code>
                  </p>
                </div>

                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Slow Network / SSH
                  </Heading>
                  <p className="text-gray-300 text-sm">
                    <code>--color-mode 16</code> or{" "}
                    <code>--color-mode 256</code>
                  </p>
                </div>

                <div className="card-standard accent-purple">
                  <Heading
                    level={4}
                    className="text-purple-300 font-semibold mb-2"
                  >
                    Lightweight / Scripting
                  </Heading>
                  <p className="text-gray-300 text-sm">
                    <code>--color false -x 80 -y 24</code>
                  </p>
                </div>
              </div>
              <p className="text-gray-400 text-sm mt-3">
                See also:{" "}
                <TrackedLink
                  to="/docs/display"
                  label="display common setups"
                  className="link-standard"
                >
                  Display common setups
                </TrackedLink>{" "}
                for render mode, palette, and FPS combinations.
              </p>
            </div>
          </section>

          <Footer />
        </div>
      </div>
    </>
  );
}
