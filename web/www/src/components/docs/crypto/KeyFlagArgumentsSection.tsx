import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function KeyFlagArgumentsSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-orange-400 mb-6 border-b border-orange-900/50 pb-2"
      >
        🗝️ Key Flag Argument Values
      </Heading>

      <p className="text-gray-300 mb-6">
        The key flags accept different argument formats depending on whether
        they take a <strong className="text-orange-300">private key</strong>{" "}
        (your identity) or a{" "}
        <strong className="text-blue-300">public key</strong> (verifying someone
        else).
      </p>

      <div className="space-y-8">
        <div>
          <Heading
            level={3}
            className="text-2xl font-semibold text-orange-400 mb-4"
          >
            Private key: --key
          </Heading>
          <p className="text-gray-300 mb-4">
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --key
            </code>{" "}
            is your <strong className="text-orange-300">identity</strong> — it
            points to a local private key that proves who you are. It only
            accepts local file paths and GPG fingerprints.
          </p>
          <CodeBlock language="bash">{`# SSH Ed25519 private key file (most common)
ascii-chat server --key ~/.ssh/id_ed25519

# GPG Ed25519 key by fingerprint (uses gpg-agent)
ascii-chat server --key gpg:897607FA43DC66F6
ascii-chat server --key gpg:7FE90A79F2E80ED3`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-2xl font-semibold text-blue-400 mb-4"
          >
            Public keys: --server-key, --client-keys, --discovery-service-key
          </Heading>
          <p className="text-gray-300 mb-4">
            These flags accept{" "}
            <strong className="text-blue-300">public key material</strong> for
            verifying someone else's identity. They all support the same rich
            set of formats:
          </p>

          <div className="space-y-6">
            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-cyan-300 mb-3"
              >
                Local files and inline keys
              </Heading>
              <CodeBlock language="bash">{`# SSH public key file
--server-key ~/.ssh/known_server.pub

# Raw base64 public key (inline)
--server-key "AAAAC3NzaC1lZDI1NTE5AAAAI..."

# One-per-line file for client whitelisting
--client-keys /path/to/allowed_clients.txt`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-purple-300 mb-3"
              >
                GitHub and GitLab
              </Heading>
              <CodeBlock language="bash">{`# GitHub SSH keys (fetches all Ed25519 keys for user)
--server-key github:agentwaj

# GitHub GPG keys
--server-key github:ethernetdan.gpg

# GitLab SSH/GPG keys
--server-key gitlab:zfogg
--server-key gitlab:agentwaj.gpg`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-teal-300 mb-3"
              >
                GPG keyring and HTTPS URLs
              </Heading>
              <CodeBlock language="bash">{`# GPG key from local keyring
--server-key gpg:897607FA43DC66F6

# HTTPS URL to a public key
--server-key https://example.com/server.pub`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-pink-300 mb-3"
              >
                Multiple keys (--client-keys)
              </Heading>
              <CodeBlock language="bash">{`# Comma-separated list mixing formats
--client-keys github:agentwaj,github:ethernetdan,gpg:AABBCCDD

# File with one key per line (authorized_keys format)
--client-keys /path/to/allowed_clients.txt`}</CodeBlock>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
