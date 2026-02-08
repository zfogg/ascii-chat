import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function Configuration() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Configuration", path: "/docs/configuration" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Configuration - ascii-chat Documentation"
        description="Learn about ascii-chat configuration: config files, command-line options, color schemes, and shell completions."
        url="https://ascii-chat.com/docs/configuration"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <h1 className="heading-1 mb-4">
              <span className="text-purple-400">⚙️</span> Configuration
            </h1>
            <p className="text-lg sm:text-xl text-gray-300">
              Config files, locations, overrides, options, and shell completions
            </p>
          </header>

          {/* Priority & Overrides */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-cyan-400">
              📊 Configuration Priority & Override Chain
            </h2>
            <p className="docs-paragraph">
              Settings are applied in strict priority order. Each level can
              override levels below it:
            </p>
            <div className="space-y-3 mb-6">
              <div className="card-standard accent-pink">
                <div className="flex items-start gap-3">
                  <span className="text-lg font-bold text-pink-400 min-w-fit">
                    1️⃣ Highest
                  </span>
                  <div>
                    <h4 className="text-pink-300 font-semibold">
                      Command-line Flags
                    </h4>
                    <p className="text-gray-400 text-sm">
                      Explicitly passed arguments (e.g.,{" "}
                      <code className="text-gray-300">--port 8080</code>). Most
                      explicit and immediate input.
                    </p>
                  </div>
                </div>
              </div>
              <div className="card-standard accent-cyan">
                <div className="flex items-start gap-3">
                  <span className="text-lg font-bold text-cyan-400 min-w-fit">
                    2️⃣ High
                  </span>
                  <div>
                    <h4 className="text-cyan-300 font-semibold">
                      Environment Variables
                    </h4>
                    <p className="text-gray-400 text-sm">
                      Set in shell (e.g.,{" "}
                      <code className="text-gray-300">
                        export ASCII_CHAT_PORT=8080
                      </code>
                      ). Session-wide settings.
                    </p>
                  </div>
                </div>
              </div>
              <div className="card-standard accent-purple">
                <div className="flex items-start gap-3">
                  <span className="text-lg font-bold text-purple-400 min-w-fit">
                    3️⃣ Medium
                  </span>
                  <div>
                    <h4 className="text-purple-300 font-semibold">
                      Config File Values
                    </h4>
                    <p className="text-gray-400 text-sm">
                      Settings from TOML configuration files
                    </p>
                  </div>
                </div>
              </div>
              <div className="card-standard accent-teal">
                <div className="flex items-start gap-3">
                  <span className="text-lg font-bold text-teal-400 min-w-fit">
                    4️⃣ Lowest
                  </span>
                  <div>
                    <h4 className="text-teal-300 font-semibold">
                      Built-in Defaults
                    </h4>
                    <p className="text-gray-400 text-sm">
                      Hard-coded default values
                    </p>
                  </div>
                </div>
              </div>
            </div>
            <div className="info-box-note">
              <p className="text-gray-300 text-sm mb-2">
                <strong>Example Override Chain:</strong>
              </p>
              <p className="text-gray-400 text-sm">
                Built-in default:{" "}
                <code className="text-cyan-300">port = 27224</code> → Config
                file: <code className="text-cyan-300">port = 8080</code> → Env
                var: <code className="text-cyan-300">ASCII_CHAT_PORT=9000</code>{" "}
                → CLI flag: <code className="text-cyan-300">--port 9999</code> =
                <strong>Final result: 9999</strong>
              </p>
            </div>
          </section>

          {/* Configuration Files & Search Paths */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-purple-400">
              📁 Configuration Files & Search Paths
            </h2>

            <p className="docs-paragraph">
              ascii-chat searches for configuration files across multiple
              standard locations. Files are loaded in order, with later files
              overriding earlier ones.
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-4">Linux / macOS</h3>
              <p className="text-gray-400 text-sm mb-3">
                Searched in order (first found is used, remaining are skipped):
              </p>
              <CodeBlock language="bash">
                {
                  "$XDG_CONFIG_HOME/ascii-chat/config.toml      (if XDG_CONFIG_HOME set)\n~/.config/ascii-chat/config.toml              (XDG Base Directory standard)\n/opt/homebrew/etc/ascii-chat/config.toml      (Homebrew - macOS)\n/usr/local/etc/ascii-chat/config.toml         (Unix local)\n/etc/ascii-chat/config.toml                   (system-wide)"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Tip:</strong> If{" "}
                  <code className="text-cyan-300">XDG_CONFIG_HOME</code> isn't
                  set, it defaults to{" "}
                  <code className="text-cyan-300">~/.config</code>
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-4">Windows</h3>
              <p className="text-gray-400 text-sm mb-3">Searched in order:</p>
              <CodeBlock language="bash">
                {
                  "%APPDATA%\\ascii-chat\\config.toml           (user config)\n%PROGRAMDATA%\\ascii-chat\\config.toml       (system-wide)\n%USERPROFILE%\\.ascii-chat\\config.toml      (legacy fallback)"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-4">
                Create a Config File
              </h3>
              <p className="text-gray-400 text-sm mb-3">
                Generate a template config file with all options commented out:
              </p>
              <CodeBlock language="bash">
                {
                  "# Creates config in standard location\nascii-chat --config-create\n\n# Or specify custom path\nascii-chat --config-create ~/.config/ascii-chat/custom.toml"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Config File Format */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-green-400">📝 Config File Format</h2>

            <p className="docs-paragraph">
              Configuration files use <strong>TOML format</strong>. Each section
              in the config file corresponds to a section in{" "}
              <code className="text-cyan-300">ascii-chat --help</code>.
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-4">
                Understanding Config Sections & Keys
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-green">
                  <h4 className="text-green-300 font-semibold mb-2">
                    TOML Sections: Uppercase → Lowercase
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Each <code className="text-cyan-300">[section]</code> in the
                    config corresponds to a section from{" "}
                    <code className="text-cyan-300">--help</code> output,
                    converted to lowercase:
                  </p>
                  <ul className="list-disc ml-6 text-gray-300 text-sm">
                    <li>
                      <code className="text-cyan-300">NETWORK</code> →{" "}
                      <code className="text-cyan-300">[network]</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">CLIENT</code> →{" "}
                      <code className="text-cyan-300">[client]</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">SERVER</code> →{" "}
                      <code className="text-cyan-300">[server]</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">AUDIO</code> →{" "}
                      <code className="text-cyan-300">[audio]</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">CRYPTO</code> →{" "}
                      <code className="text-cyan-300">[crypto]</code>
                      {" ("}
                      <TrackedLink
                        to="/crypto"
                        label="crypto config"
                        className="text-cyan-400 hover:text-cyan-300 transition-colors"
                      >
                        encryption details
                      </TrackedLink>
                      {")"}
                    </li>
                  </ul>
                </div>
                <div className="card-standard accent-green">
                  <h4 className="text-green-300 font-semibold mb-2">
                    Config Keys: Flags → Underscores
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Keys are CLI flags with hyphens converted to underscores:
                  </p>
                  <ul className="list-disc ml-6 text-gray-300 text-sm">
                    <li>
                      <code className="text-cyan-300">--color-mode</code> →{" "}
                      <code className="text-cyan-300">color_mode</code>
                      {" ("}
                      <TrackedLink
                        to="/docs/terminal"
                        label="color-mode config"
                        className="text-cyan-400 hover:text-cyan-300 transition-colors"
                      >
                        see Terminal docs
                      </TrackedLink>
                      {")"}
                    </li>
                    <li>
                      <code className="text-cyan-300">--render-mode</code> →{" "}
                      <code className="text-cyan-300">render_mode</code>
                      {" ("}
                      <TrackedLink
                        to="/docs/terminal"
                        label="render-mode config"
                        className="text-cyan-400 hover:text-cyan-300 transition-colors"
                      >
                        see Terminal docs
                      </TrackedLink>
                      {")"}
                    </li>
                    <li>
                      <code className="text-cyan-300">--no-encrypt</code> →{" "}
                      <code className="text-cyan-300">no_encrypt</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">--bind-ipv4</code> →{" "}
                      <code className="text-cyan-300">bind_ipv4</code>
                    </li>
                    <li>
                      <code className="text-cyan-300">--max-clients</code> →{" "}
                      <code className="text-cyan-300">max_clients</code>
                    </li>
                  </ul>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-4">
                Complete Example Config
              </h3>
              <p className="text-gray-400 text-sm mb-3">
                This example demonstrates all major configuration sections:
              </p>
              <CodeBlock language="bash">{`# ===== NETWORK =====
[network]
port = 27224

# ===== SERVER =====
[server]
bind_ipv4 = "127.0.0.1"
bind_ipv6 = "::1"
max_clients = 32

# ===== CLIENT =====
[client]
address = "localhost:27224"
width = 110
height = 70
color_mode = "auto"              # see /docs/terminal
render_mode = "foreground"       # see /docs/terminal
fps = 60
webcam_index = 0                 # see /docs/hardware
webcam_flip = false              # see /docs/hardware
stretch = false

# ===== AUDIO ===== (see /docs/hardware)
[audio]
enabled = false
device = -1

# ===== PALETTE & RENDERING =====
[palette]
type = "standard"

# ===== CRYPTOGRAPHY =====
[crypto]
encrypt_enabled = true
key = "~/.ssh/id_ed25519"
password = "my-secret"
no_encrypt = false
server_key = "~/.ssh/server_key.pub"

# ===== LOGGING =====
[logging]
log_file = "/tmp/ascii-chat.log"`}</CodeBlock>
            </div>

            <p className="docs-paragraph text-sm mt-4 text-gray-400">
              <strong>Tip:</strong> Only include the sections and keys you want
              to customize. All unspecified options use built-in defaults. See{" "}
              <code className="text-cyan-300">ascii-chat --help</code> for all
              available options.
            </p>
          </section>

          {/* Environment Variables */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-cyan-400">
              🌍 Environment Variables
            </h2>

            <div className="info-box-info mb-6">
              <p className="text-gray-300 text-sm mb-2">
                <strong>📖 Complete Reference:</strong>
              </p>
              <p className="text-gray-400 text-sm">
                For the complete list of all{" "}
                <code className="text-cyan-300">ASCII_CHAT_*</code> environment
                variables and their descriptions, see the{" "}
                <strong>ENVIRONMENT</strong> section of the{" "}
                <a
                  href="/man1#ENVIRONMENT"
                  className="text-cyan-400 hover:text-cyan-300 transition-colors"
                >
                  <code className="text-cyan-300">ascii-chat(1)</code>
                </a>{" "}
                man page.
              </p>
            </div>

            <p className="docs-paragraph">
              Environment variables provide session-wide configuration and are
              the second level in the priority chain. CLI flags override
              environment variables. They serve two purposes:{" "}
              <strong>(1)</strong> overriding option values (when not overridden
              by CLI flags), and <strong>(2)</strong> discovering config file
              locations.
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-4">
                Option Override Variables
              </h3>
              <p className="text-gray-400 text-sm mb-3">
                Most CLI options have a corresponding environment variable.
                Environment variables provide session-wide configuration that
                can be overridden by explicit CLI flags. For a complete list of
                all available{" "}
                <code className="text-cyan-300">ASCII_CHAT_*</code> variables,
                see the <code className="text-cyan-300">ENVIRONMENT</code>{" "}
                section of the{" "}
                <a
                  href="/man1#ENVIRONMENT"
                  className="text-cyan-400 hover:text-cyan-300 transition-colors"
                >
                  <code className="text-cyan-300">ascii-chat(1)</code>
                </a>{" "}
                man page.
              </p>
              <div className="space-y-3">
                <div className="card-standard accent-pink">
                  <h4 className="text-pink-300 font-semibold mb-2">
                    Naming Convention
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Environment variable names are uppercase option names with
                    hyphens converted to underscores, prefixed with{" "}
                    <code className="text-cyan-300">ASCII_CHAT_</code>:
                  </p>
                  <ul className="list-disc ml-6 text-gray-300 text-sm space-y-1">
                    <li>
                      <code className="text-cyan-300">--port</code> →{" "}
                      <code className="text-cyan-300">
                        ASCII_CHAT_PORT=8080
                      </code>
                    </li>
                    <li>
                      <code className="text-cyan-300">--color-mode</code> →{" "}
                      <code className="text-cyan-300">
                        ASCII_CHAT_COLOR_MODE=truecolor
                      </code>
                    </li>
                    <li>
                      <code className="text-cyan-300">--width</code> →{" "}
                      <code className="text-cyan-300">
                        ASCII_CHAT_WIDTH=120
                      </code>
                    </li>
                    <li>
                      <code className="text-cyan-300">--fps</code> →{" "}
                      <code className="text-cyan-300">ASCII_CHAT_FPS=60</code>
                    </li>
                  </ul>
                  <p className="text-gray-400 text-sm mt-3">
                    View <code className="text-cyan-300">man ascii-chat</code>{" "}
                    or the <code className="text-cyan-300">ascii-chat(1)</code>{" "}
                    documentation for the complete list of all supported options
                    and their corresponding environment variables.
                  </p>
                </div>
                <div className="card-standard accent-pink">
                  <h4 className="text-pink-300 font-semibold mb-2">
                    Example: Override Chain in Action
                  </h4>
                  <CodeBlock language="bash">{`# config.toml:
[network]
port = 8080

[client]
width = 80
color_mode = "auto"

# Set environment variables:
$ export ASCII_CHAT_PORT=9000
$ export ASCII_CHAT_WIDTH=120
$ export ASCII_CHAT_COLOR_MODE=truecolor

# Run with CLI flag (CLI flag takes precedence):
$ ascii-chat client --port 8888

# Final configuration:
port = 8888            # from --port CLI flag (HIGHEST priority)
width = 120            # from ASCII_CHAT_WIDTH env var (overrides config)
color_mode = truecolor # from ASCII_CHAT_COLOR_MODE env var (overrides config)
`}</CodeBlock>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-4">
                Config File Discovery Variables
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-1">
                    XDG_CONFIG_HOME
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Specifies config directory location (Linux/macOS)
                  </p>
                  <CodeBlock language="bash">
                    export XDG_CONFIG_HOME=~/.config
                  </CodeBlock>
                </div>
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-1">
                    APPDATA / USERPROFILE
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Used for config location on Windows (set by system)
                  </p>
                </div>
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-1">HOME</h4>
                  <p className="text-gray-400 text-sm mb-2">
                    User home directory
                  </p>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-4">
                Special Purpose Variables
              </h3>
              <p className="text-gray-400 text-sm mb-4">
                Beyond the standard{" "}
                <code className="text-cyan-300">ASCII_CHAT_*</code> option
                variables, ascii-chat recognizes these system variables:
              </p>
              <div className="space-y-3">
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    SSH_AUTH_SOCK
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    SSH agent socket for password-free key authentication (Unix
                    only)
                  </p>
                  <p className="text-gray-400 text-sm">
                    Set by ssh-agent automatically. Required for encrypted SSH
                    keys.
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    GNUPGHOME
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    GPG home directory for key management and gpg-agent
                  </p>
                  <p className="text-gray-400 text-sm">
                    Defaults to <code className="text-cyan-300">~/.gnupg</code>{" "}
                    if not set
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    ASCII_CHAT_KEY_PASSWORD
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    ⚠️ Passphrase for encrypted SSH/GPG keys
                  </p>
                  <p className="text-gray-400 text-sm text-red-400 mb-2">
                    <strong>SECURITY WARNING:</strong> Only use in CI/CD
                    environments, never commit to version control
                  </p>
                  <p className="text-gray-400 text-sm">
                    Example:{" "}
                    <code className="text-cyan-300">
                      export ASCII_CHAT_KEY_PASSWORD="my-passphrase"
                    </code>
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    WEBCAM_DISABLED
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Use test pattern instead of real webcam
                  </p>
                  <p className="text-gray-400 text-sm">
                    Equivalent to{" "}
                    <code className="text-cyan-300">--test-pattern</code> CLI
                    flag
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    XDG_CONFIG_DIRS
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Additional system-wide config directories (Linux/macOS)
                  </p>
                  <p className="text-gray-400 text-sm">
                    Colon-separated paths (default:{" "}
                    <code className="text-cyan-300">/etc/xdg</code>)
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    XDG_DATA_HOME
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    XDG Base Directory data location (Linux/macOS)
                  </p>
                  <p className="text-gray-400 text-sm">
                    Defaults to{" "}
                    <code className="text-cyan-300">~/.local/share</code> if not
                    set
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    USER / USERNAME
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Current user identifier (system-set)
                  </p>
                  <p className="text-gray-400 text-sm">
                    Used for TOFU verification and user identification. Set by
                    OS automatically.
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-1">
                    LLVM_SYMBOLIZER_PATH &amp; _NT_SYMBOL_PATH
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    Debug build helpers for stack traces (debug builds only)
                  </p>
                  <p className="text-gray-400 text-sm">
                    Improves crash report quality in debug builds
                  </p>
                </div>
              </div>
            </div>
          </section>

          {/* Man Page */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-teal-400">📖 Man Pages</h2>

            <p className="docs-paragraph">
              The complete reference documentation for ascii-chat is available
              as traditional Unix man pages:
            </p>

            <div className="space-y-3 mb-6">
              <div className="card-standard accent-teal">
                <h4 className="text-teal-300 font-semibold mb-2">
                  <a
                    href="/man1"
                    className="text-teal-300 hover:text-teal-200 transition-colors"
                  >
                    ascii-chat(1)
                  </a>{" "}
                  - Command Reference
                </h4>
                <p className="text-gray-400 text-sm">
                  Complete list of all command-line options, environment
                  variables, modes, examples, security details, and keyboard
                  controls.
                </p>
              </div>
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  <a
                    href="/man5"
                    className="text-purple-300 hover:text-purple-200 transition-colors"
                  >
                    ascii-chat(5)
                  </a>{" "}
                  - File Formats
                </h4>
                <p className="text-gray-400 text-sm">
                  Configuration file format (TOML), color scheme files,
                  authentication files, and other data formats.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-4">View Man Pages</h3>
              <p className="text-gray-400 text-sm mb-3">
                View ascii-chat documentation in your terminal:
              </p>
              <CodeBlock language="bash">
                {`man ascii-chat    # Command reference (section 1)
man 5 ascii-chat  # File formats (section 5)`}
              </CodeBlock>
              <p className="text-gray-400 text-sm mt-3 mb-3">
                Or view them on the web:
              </p>
              <CodeBlock language="bash">
                {`open https://ascii-chat.com/man1  # Command reference
open https://ascii-chat.com/man5  # File formats`}
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-4">
                Install Man Page for Terminal Access
              </h3>

              <p className="text-gray-400 text-sm mb-4">
                By default, the ascii-chat man page is installed in the standard
                system location. If{" "}
                <code className="text-cyan-300">man ascii-chat</code> doesn't
                work, you may need to add the installation directory to your{" "}
                <code className="text-cyan-300">MANPATH</code>.
              </p>

              <div className="docs-subsection-spacing">
                <h4 className="text-teal-300 font-semibold mb-3">
                  macOS (Homebrew)
                </h4>
                <p className="text-gray-400 text-sm mb-2">
                  If you installed ascii-chat via Homebrew, add the man page
                  directory to your shell profile:
                </p>
                <CodeBlock language="bash">{`# Add to ~/.zshrc or ~/.bashrc
export MANPATH="$(brew --prefix)/share/man:$MANPATH"`}</CodeBlock>
                <p className="text-gray-400 text-sm mt-2 mb-3">
                  Then reload your shell:
                </p>
                <CodeBlock language="bash">
                  source ~/.zshrc # or source ~/.bashrc
                </CodeBlock>
              </div>

              <div className="docs-subsection-spacing">
                <h4 className="text-teal-300 font-semibold mb-3">Linux</h4>
                <p className="text-gray-400 text-sm mb-2">
                  The man page is typically installed to{" "}
                  <code className="text-cyan-300">/usr/local/share/man</code> or{" "}
                  <code className="text-cyan-300">/usr/share/man</code>. These
                  are usually in your{" "}
                  <code className="text-cyan-300">MANPATH</code> by default.
                </p>
                <p className="text-gray-400 text-sm">
                  If not, add to your shell profile:
                </p>
                <CodeBlock language="bash">{`export MANPATH="/usr/local/share/man:$MANPATH"`}</CodeBlock>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-4">
                Generate Man Page Manually
              </h3>
              <p className="text-gray-400 text-sm mb-3">
                Generate the man page from the binary:
              </p>
              <CodeBlock language="bash">
                {"ascii-chat --man-page-create > /tmp/ascii-chat.1"}
              </CodeBlock>
              <p className="text-gray-400 text-sm mt-3">Then view it with:</p>
              <CodeBlock language="bash">man /tmp/ascii-chat.1</CodeBlock>
            </div>

            <div className="info-box-info mt-4">
              <p className="text-gray-300 text-sm">
                <strong>📖 Complete Reference:</strong> The man page includes
                comprehensive sections on OPTIONS, ENVIRONMENT variables (all{" "}
                <code className="text-cyan-300">ASCII_CHAT_*</code> variables),
                EXAMPLES, SECURITY, KEYBOARD CONTROLS, and more.
              </p>
            </div>
          </section>

          {/* Shell Completions */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-green-400">🐚 Shell Completions</h2>
            <p className="docs-paragraph">
              Generate shell completions for bash, fish, or zsh:
            </p>
            <CodeBlock language="bash">
              {
                "ascii-chat --completions bash > ~/.bash_completion.d/ascii-chat\nascii-chat --completions fish > ~/.config/fish/completions/ascii-chat.fish\nascii-chat --completions zsh > ~/.zfunc/_ascii-chat"
              }
            </CodeBlock>
          </section>

          <Footer />
        </div>
      </div>
    </>
  );
}
