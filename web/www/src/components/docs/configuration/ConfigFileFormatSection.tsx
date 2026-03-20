import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";

export default function ConfigFileFormatSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        📝 Config File Format
      </Heading>

      <p className="docs-paragraph">
        Configuration files use <strong>TOML format</strong>. Each section in
        the config file corresponds to a section in{" "}
        <code className="text-cyan-300">ascii-chat --help</code>.
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-4">
          Understanding Config Sections & Keys
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-green">
            <Heading level={4} className="text-green-300 font-semibold mb-2">
              TOML Sections: Uppercase → Lowercase
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              Each <code className="text-cyan-300">[section]</code> in the
              config corresponds to a section from{" "}
              <code className="text-cyan-300">--help</code> output, converted to
              lowercase:
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
                  to="/docs/crypto"
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
            <Heading level={4} className="text-green-300 font-semibold mb-2">
              Config Keys: Flags → Underscores
            </Heading>
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
        <Heading level={3} className="heading-3 text-green-300 mb-4">
          Complete Example Config
        </Heading>
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
        <strong>Tip:</strong> Only include the sections and keys you want to
        customize. All unspecified options use built-in defaults. See{" "}
        <code className="text-cyan-300">ascii-chat --help</code> for all
        available options.
      </p>
    </section>
  );
}
