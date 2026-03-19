import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function EnvironmentVariablesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        🌍 Environment Variables
      </Heading>

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
        <Heading level={3} className="heading-3 text-pink-300 mb-4">
          Option Override Variables
        </Heading>
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
            <Heading
              level={4}
              className="text-pink-300 font-semibold mb-2"
            >
              Naming Convention
            </Heading>
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
            <Heading
              level={4}
              className="text-pink-300 font-semibold mb-2"
            >
              Example: Override Chain in Action
            </Heading>
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
        <Heading level={3} className="heading-3 text-teal-300 mb-4">
          Config File Discovery Variables
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-1"
            >
              XDG_CONFIG_HOME
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              Specifies config directory location (Linux/macOS)
            </p>
            <CodeBlock language="bash">
              export XDG_CONFIG_HOME=~/.config
            </CodeBlock>
          </div>
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-1"
            >
              APPDATA / USERPROFILE
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              Used for config location on Windows (set by system)
            </p>
          </div>
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-1"
            >
              HOME
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              User home directory
            </p>
          </div>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-4">
          Special Purpose Variables
        </Heading>
        <p className="text-gray-400 text-sm mb-4">
          Beyond the standard{" "}
          <code className="text-cyan-300">ASCII_CHAT_*</code> option
          variables, ascii-chat recognizes these system variables:
        </p>
        <div className="space-y-3">
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              SSH_AUTH_SOCK
            </Heading>
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
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              GNUPGHOME
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              GPG home directory for key management and gpg-agent
            </p>
            <p className="text-gray-400 text-sm">
              Defaults to <code className="text-cyan-300">~/.gnupg</code>{" "}
              if not set
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              ASCII_CHAT_KEY_PASSWORD
            </Heading>
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
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              WEBCAM_DISABLED
            </Heading>
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
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              XDG_CONFIG_DIRS
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              Additional system-wide config directories (Linux/macOS)
            </p>
            <p className="text-gray-400 text-sm">
              Colon-separated paths (default:{" "}
              <code className="text-cyan-300">/etc/xdg</code>)
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              XDG_DATA_HOME
            </Heading>
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
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              USER / USERNAME
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              Current user identifier (system-set)
            </p>
            <p className="text-gray-400 text-sm">
              Used for TOFU verification and user identification. Set by
              OS automatically.
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-1"
            >
              LLVM_SYMBOLIZER_PATH &amp; _NT_SYMBOL_PATH
            </Heading>
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
  );
}
