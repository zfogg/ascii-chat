import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ConfigFilesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        📁 Configuration Files & Search Paths
      </Heading>

      <p className="docs-paragraph">
        ascii-chat searches for configuration files across multiple
        standard locations. Files are loaded in order, with later files
        overriding earlier ones.
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-4">
          Linux / macOS
        </Heading>
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
        <Heading level={3} className="heading-3 text-pink-300 mb-4">
          Windows
        </Heading>
        <p className="text-gray-400 text-sm mb-3">Searched in order:</p>
        <CodeBlock language="bash">
          {
            "%APPDATA%\\ascii-chat\\config.toml           (user config)\n%PROGRAMDATA%\\ascii-chat\\config.toml       (system-wide)\n%USERPROFILE%\\.ascii-chat\\config.toml      (legacy fallback)"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-4">
          Create a Config File
        </Heading>
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
  );
}
