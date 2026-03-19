import { Heading } from "@ascii-chat/shared/components";

export default function PriorityOverridesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        📊 Configuration Priority & Override Chain
      </Heading>
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
              <Heading level={4} className="text-pink-300 font-semibold">
                Command-line Flags
              </Heading>
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
              <Heading level={4} className="text-cyan-300 font-semibold">
                Environment Variables
              </Heading>
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
              <Heading
                level={4}
                className="text-purple-300 font-semibold"
              >
                Config File Values
              </Heading>
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
              <Heading level={4} className="text-teal-300 font-semibold">
                Built-in Defaults
              </Heading>
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
  );
}
