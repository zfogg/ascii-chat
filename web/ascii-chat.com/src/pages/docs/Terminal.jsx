import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";

export default function Terminal() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Terminal", path: "/docs/terminal" },
    ]);
  }, []);
  return (
    <div className="bg-gray-950 text-gray-100 flex flex-col">
      <div className="flex-1 flex flex-col docs-container">
        <header className="mb-12 sm:mb-16">
          <h1 className="heading-1 mb-4">
            <span className="text-cyan-400">🖥️</span> Terminal Rendering
          </h1>
          <p className="text-lg sm:text-xl text-gray-300">
            Colors, display modes, dimensions, Unicode support, and terminal
            capabilities for perfect rendering
          </p>
        </header>

        {/* Important Notes */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-cyan-400">📌 Important Notes</h2>

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
                <code>$ROWS</code> environment variables (or terminal queries).
                Override with <code>-x</code> and <code>-y</code>.
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
          <h2 className="heading-2 text-purple-400">🎨 Color Control</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">
              Enable/Disable Color
            </h3>
            <p className="docs-paragraph">
              The <code>--color</code> flag controls whether colors are
              displayed at all (independent of color mode):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Auto-detect (DEFAULT)\nascii-chat client example.com --color auto\n\n# Force colors on (even when piped)\nascii-chat mirror --color true | tee output.txt\n\n# Force colors off (monochrome)\nascii-chat client --color false\n\n# Via environment variable\nASCII_CHAT_COLOR=false ascii-chat client"
                }
              </code>
            </pre>
            <div className="info-box-info mt-3">
              <p className="text-gray-300 text-sm">
                <strong>When colors are disabled:</strong> Display switches to
                monochrome (black and white). Piping automatically disables
                colors unless <code>--color true</code> is used.
              </p>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">Color Depth Modes</h3>
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
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Auto-detect (RECOMMENDED)\nascii-chat client --color-mode auto\n\n# Force specific color mode\nascii-chat client --color-mode truecolor  # 16M colors (best quality)\nascii-chat client --color-mode 256        # 256-color xterm\nascii-chat client --color-mode 16         # Basic ANSI colors\nascii-chat client --color-mode none       # Monochrome\n\n# Via environment\nexport ASCII_CHAT_COLOR_MODE=256"
                }
              </code>
            </pre>
            <div className="space-y-2 mt-3">
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Color Mode Details
                </h4>
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
                    <strong>16:</strong> Basic 16 ANSI colors (slow connections)
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
            <h3 className="heading-3 text-teal-300 mb-3">
              Logging Color Scheme
            </h3>
            <p className="docs-paragraph">
              Control the color scheme for ascii-chat's debug/log output (not
              video display):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Available schemes: pastel, nord, solarized-dark, dracula, gruvbox-dark, monokai, etc.\nascii-chat client --color-scheme dracula\n\n# Via environment\nexport ASCII_CHAT_COLOR_SCHEME=nord"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Display Modes */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-green-400">📐 Display Modes</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">Render Modes</h3>
            <p className="docs-paragraph">
              Control how ASCII characters are rendered to represent brightness:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Foreground (DEFAULT) - colored text on dark background\nascii-chat client --render-mode foreground\n\n# Background - colored background with white text\nascii-chat client --render-mode background\n\n# Half-block - Unicode blocks for 2x vertical resolution\nascii-chat mirror --render-mode half-block\n\n# Short form\nascii-chat mirror -M half-block"
                }
              </code>
            </pre>
            <div className="space-y-2 mt-3">
              <div className="card-standard accent-green">
                <h4 className="text-green-300 font-semibold mb-2">
                  Render Mode Comparison
                </h4>
                <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                  <li>
                    <strong>foreground:</strong> Classic look, good
                    compatibility
                  </li>
                  <li>
                    <strong>background:</strong> Better for light terminals
                  </li>
                  <li>
                    <strong>half-block:</strong> Double vertical resolution,
                    requires UTF-8
                  </li>
                </ul>
              </div>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">ASCII Palettes</h3>
            <p className="docs-paragraph">
              Control which ASCII characters represent different brightness
              levels:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  '# Built-in palettes\nascii-chat mirror --palette standard   # Default\nascii-chat mirror --palette blocks      # Unicode blocks\nascii-chat mirror --palette digital     # Digital style\nascii-chat mirror --palette minimal     # Minimal detail\nascii-chat mirror --palette cool        # Alternative\n\n# Custom palette (darkest to brightest)\nascii-chat mirror --palette custom --palette-chars " .:-=+*#%@"'
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-yellow-300 mb-3">Aspect Ratio</h3>
            <p className="docs-paragraph">
              By default, ascii-chat preserves aspect ratio (terminal characters
              are ~2:1 height:width). Use <code>--stretch</code> to ignore
              aspect ratio and fill the terminal:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Preserve aspect ratio (DEFAULT)\nascii-chat mirror video.mp4\n\n# Stretch to fill terminal (may distort)\nascii-chat mirror video.mp4 --stretch"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Terminal Dimensions */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-teal-400">📏 Terminal Dimensions</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">
              Auto-Detection & Environment Variables
            </h3>
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
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Set dimensions via environment variables\nexport COLUMNS=120 ROWS=40\nascii-chat client example.com\n\n# Or set just for this command\nCOLUMNS=150 ROWS=50 ascii-chat mirror video.mp4"
                }
              </code>
            </pre>
            <div className="info-box-info mt-3">
              <p className="text-gray-300 text-sm">
                <strong>Auto-Update:</strong> When running interactively (not
                piped), dimensions are updated in real-time when you resize the
                terminal.
              </p>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">Custom Dimensions</h3>
            <p className="docs-paragraph">
              Override auto-detection with explicit width and height:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Set custom dimensions\nascii-chat client --width 120 --height 40\n\n# Short form\nascii-chat client -x 120 -y 40\n\n# Very wide for multiple columns\nascii-chat mirror -x 200 -y 60\n\n# Minimal size\nascii-chat client -x 20 -y 10\n\n# Via environment variables\nexport ASCII_CHAT_WIDTH=150 ASCII_CHAT_HEIGHT=50"
                }
              </code>
            </pre>
            <div className="info-box-note mt-3">
              <p className="text-gray-300 text-sm">
                <strong>Valid Range:</strong> width 20-512, height 10-256
              </p>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">
              Piping & Non-Interactive Mode
            </h3>
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
                Always use <code>-x</code>/<code>-y</code> for consistent output
              </li>
            </ul>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Capture single frame to file with explicit size\nascii-chat mirror -x 120 -y 40 --snapshot -D 0 > frame.txt\n\n# Pipe to file with consistent dimensions\nascii-chat mirror -x 100 -y 30 | tee output.txt\n\n# Use with COLUMNS/ROWS for piped output\nCOLUMNS=200 ROWS=60 ascii-chat mirror | cat"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Unicode & Text */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-indigo-400">🌐 Unicode & Text</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-indigo-300 mb-3">UTF-8 Support</h3>
            <p className="docs-paragraph">
              Control UTF-8/Unicode character support for palettes and output:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Auto-detect (DEFAULT) - based on locale\nascii-chat mirror --utf8 auto\n\n# Force UTF-8 on\nascii-chat mirror --utf8 true --palette blocks\n\n# Force UTF-8 off (ASCII-only)\nascii-chat mirror --utf8 false\n\n# Via environment\nexport ASCII_CHAT_UTF8=true"
                }
              </code>
            </pre>
            <div className="info-box-warning mt-3">
              <p className="text-gray-300 text-sm">
                <strong>⚠️ Warning:</strong> Some palettes (blocks, half-block)
                require UTF-8. They'll fall back to ASCII if UTF-8 is disabled.
              </p>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-blue-300 mb-3">Strip ANSI Codes</h3>
            <p className="docs-paragraph">
              Remove ANSI escape sequences from output (for saving to files or
              processing text):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Capture plain text (no colors/formatting)\nascii-chat mirror --snapshot -D 0 --strip-ansi > frame.txt\n\n# Pipe output and remove ANSI codes\nascii-chat mirror | ascii-chat mirror --strip-ansi | tee plain.txt\n\n# Via environment\nexport ASCII_CHAT_STRIP_ANSI=true"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Snapshot Mode */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-orange-400">📸 Snapshot Mode</h2>

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
          <h2 className="heading-2 text-pink-400">🔍 Debugging & Detection</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">
              Show Terminal Capabilities
            </h3>
            <p className="docs-paragraph">
              Display detected terminal capabilities and configuration:
            </p>
            <pre className="code-block">
              <code className="code-content">
                ascii-chat --show-capabilities
              </code>
            </pre>
            <p className="text-gray-400 text-sm mt-2">
              Output includes: color support, Unicode support, terminal size,
              isatty status, environment variables, and detected TERM type.
            </p>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">
              Debug Display Issues
            </h3>
            <div className="space-y-3">
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Colors Look Wrong
                </h4>
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
                <h4 className="text-purple-300 font-semibold mb-2">
                  Unicode Characters Broken
                </h4>
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
                <h4 className="text-purple-300 font-semibold mb-2">
                  Dimensions Wrong
                </h4>
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
          <h2 className="heading-2 text-blue-400">🔗 Option Combinations</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-blue-300 mb-3">
              Render Mode & Display Dependencies
            </h3>
            <div className="space-y-2">
              <div className="card-standard accent-blue">
                <h4 className="text-blue-300 font-semibold mb-2">
                  half-block Requires UTF-8
                </h4>
                <p className="text-gray-300 text-sm">
                  <strong>Combination:</strong>{" "}
                  <code>--render-mode half-block</code> +{" "}
                  <code>--utf8 true</code>
                </p>
              </div>

              <div className="card-standard accent-blue">
                <h4 className="text-blue-300 font-semibold mb-2">
                  Custom Palette Requires UTF-8
                </h4>
                <p className="text-gray-300 text-sm">
                  <strong>Combination:</strong>{" "}
                  <code>--palette blocks --utf8 true</code>
                </p>
              </div>

              <div className="card-standard accent-blue">
                <h4 className="text-blue-300 font-semibold mb-2">
                  Piping Affects Color & Dimensions
                </h4>
                <p className="text-gray-300 text-sm mb-2">
                  When piped, always explicitly set:
                </p>
                <pre className="code-block text-xs mt-2">
                  <code className="code-content">
                    {`# For piped output, set all three:
ascii-chat mirror -x 120 -y 40 --color true | tee output.txt`}
                  </code>
                </pre>
              </div>
            </div>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">Common Setups</h3>
            <div className="space-y-2">
              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Maximum Quality
                </h4>
                <pre className="code-block text-xs">
                  <code className="code-content">
                    {`ascii-chat mirror \\
  --color-mode truecolor \\
  --render-mode half-block \\
  --palette blocks \\
  --utf8 true \\
  --fps 60`}
                  </code>
                </pre>
              </div>

              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Over SSH (Slow)
                </h4>
                <pre className="code-block text-xs">
                  <code className="code-content">
                    {`ascii-chat client example.com \\
  --color-mode 256 \\
  --render-mode foreground \\
  --fps 30`}
                  </code>
                </pre>
              </div>

              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Capture to File
                </h4>
                <pre className="code-block text-xs">
                  <code className="code-content">
                    {`ascii-chat mirror -x 120 -y 40 \\
  --snapshot -D 0 \\
  --strip-ansi > frame.txt`}
                  </code>
                </pre>
              </div>

              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Piped Output
                </h4>
                <pre className="code-block text-xs">
                  <code className="code-content">
                    {`COLUMNS=150 ROWS=50 ascii-chat mirror \\
  --color true \\
  --render-mode half-block | lolcat`}
                  </code>
                </pre>
              </div>
            </div>
          </div>
        </section>

        {/* Tips & Tricks */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-green-400">💡 Tips & Tricks</h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">
              Environment Variable Precedence
            </h3>
            <p className="docs-paragraph">
              Order of precedence (highest to lowest):
            </p>
            <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1 ml-2">
              <li>
                Command-line flags (<code>--width 120</code>)
              </li>
              <li>
                Environment variables (<code>export ASCII_CHAT_WIDTH=120</code>)
              </li>
              <li>Auto-detection (terminal queries, COLUMNS/ROWS)</li>
              <li>Defaults (110×70, auto color, etc.)</li>
            </ol>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              SSH & Remote Sessions
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Set TERM before SSH\nexport TERM=xterm-256color\nssh user@host ascii-chat client\n\n# Or in ~/.ssh/config\nHost *\n  SetEnv TERM=xterm-256color\n\n# Force color mode over SSH\nssh user@host ascii-chat client --color-mode 256"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-purple-300 mb-3">
              Optimal Settings by Device
            </h3>
            <div className="space-y-2">
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Modern Terminal
                </h4>
                <p className="text-gray-300 text-sm">
                  <code>
                    --color-mode truecolor --render-mode half-block --utf8 true
                  </code>
                </p>
              </div>

              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Slow Network
                </h4>
                <p className="text-gray-300 text-sm">
                  <code>--color-mode 16 --render-mode foreground --fps 30</code>
                </p>
              </div>

              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Lightweight
                </h4>
                <p className="text-gray-300 text-sm">
                  <code>--color false --fps 20 -x 80 -y 24</code>
                </p>
              </div>
            </div>
          </div>
        </section>

        <Footer />
      </div>
    </div>
  );
}
