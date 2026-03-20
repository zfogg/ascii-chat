import { Heading } from "@ascii-chat/shared/components";

export default function SecurityFlagsReferenceSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
      >
        🛡️ Security Flags Reference
      </Heading>

      <p className="text-gray-300 mb-6">
        Every security-related flag, which modes it works in, its environment
        variable, and what it does. Modes:{" "}
        <strong className="text-cyan-300">S</strong>=server,{" "}
        <strong className="text-purple-300">C</strong>=client,{" "}
        <strong className="text-teal-300">D</strong>=discovery,{" "}
        <strong className="text-pink-300">DS</strong>=discovery-service.
      </p>

      <div className="overflow-x-auto mb-8">
        <table className="w-full text-sm border-collapse">
          <thead>
            <tr className="border-b border-gray-700">
              <th className="text-left text-gray-400 py-2 pr-3">Flag</th>
              <th className="text-left text-gray-400 py-2 pr-3">Short</th>
              <th className="text-left text-gray-400 py-2 pr-3">Modes</th>
              <th className="text-left text-gray-400 py-2 pr-3">Env Variable</th>
              <th className="text-left text-gray-400 py-2">Description</th>
            </tr>
          </thead>
          <tbody className="text-gray-300">
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --encrypt
                </code>
              </td>
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  -E
                </code>
              </td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">ASCII_CHAT_ENCRYPT</code>
              </td>
              <td className="py-2 text-xs">
                Enable E2E encryption (on by default)
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --key
                </code>
              </td>
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  -K
                </code>
              </td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">ASCII_CHAT_KEY</code>
              </td>
              <td className="py-2 text-xs">
                SSH Ed25519 or GPG key file, gpg:ID, github:USER, gitlab:USER, or
                HTTPS URL
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --password
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">ASCII_CHAT_PASSWORD</code>
              </td>
              <td className="py-2 text-xs">
                Shared password for authentication (8–256 characters)
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-red-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --no-encrypt
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_NO_ENCRYPT
                </code>
              </td>
              <td className="py-2 text-xs">
                Disable encryption. Both sides must agree.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-red-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --no-auth
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">ASCII_CHAT_NO_AUTH</code>
              </td>
              <td className="py-2 text-xs">
                Disable authentication layer. Use with --no-encrypt to bypass ACIP
                entirely.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --server-key
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_SERVER_KEY
                </code>
              </td>
              <td className="py-2 text-xs">
                Expected server public key for MITM protection. Supports files,
                gpg:ID, github:USER, URLs.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --client-keys
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-teal-300">D</span>{" "}
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_CLIENT_KEYS
                </code>
              </td>
              <td className="py-2 text-xs">
                Comma-separated allowed client keys. Supports files, gpg:ID,
                github:USER, URLs.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-yellow-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --discovery-insecure
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_DISCOVERY_INSECURE
                </code>
              </td>
              <td className="py-2 text-xs">
                Skip discovery server key verification (MITM-vulnerable).
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --discovery-service-key
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-cyan-300">S</span>{" "}
                <span className="text-purple-300">C</span>{" "}
                <span className="text-teal-300">D</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_DISCOVERY_SERVER_KEY
                </code>
              </td>
              <td className="py-2 text-xs">
                Discovery server public key for verification. Same key formats as
                --key.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --require-server-identity
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_REQUIRE_SERVER_IDENTITY
                </code>
              </td>
              <td className="py-2 text-xs">
                Require servers to provide signed Ed25519 identity to register.
              </td>
            </tr>
            <tr className="border-b border-gray-800/50">
              <td className="py-2 pr-3">
                <code className="text-green-400 bg-gray-950 px-1.5 py-0.5 rounded text-xs">
                  --require-client-identity
                </code>
              </td>
              <td className="py-2 pr-3 text-gray-600 text-xs">—</td>
              <td className="py-2 pr-3 text-xs">
                <span className="text-pink-300">DS</span>
              </td>
              <td className="py-2 pr-3">
                <code className="text-gray-400 text-xs">
                  ASCII_CHAT_REQUIRE_CLIENT_IDENTITY
                </code>
              </td>
              <td className="py-2 text-xs">
                Require clients to provide signed Ed25519 identity to discover
                sessions.
              </td>
            </tr>
          </tbody>
        </table>
      </div>

      <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4">
        <p className="text-gray-300 text-sm">
          <strong className="text-cyan-300">Environment variables:</strong> All
          flags map to{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            ASCII_CHAT_FLAG_NAME
          </code>{" "}
          (hyphens become underscores). Precedence: config file {"<"} env vars{" "}
          {"<"} CLI flags.
        </p>
      </div>
    </section>
  );
}
