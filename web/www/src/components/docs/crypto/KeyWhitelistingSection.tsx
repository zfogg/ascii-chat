import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function KeyWhitelistingSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2"
      >
        👥 Key Whitelisting
      </Heading>

      <p className="text-gray-300 mb-6">
        Restrict connections to known peers by maintaining whitelists of trusted
        public keys. Works both ways: servers can whitelist clients, and clients
        can whitelist servers.
      </p>

      <div className="space-y-8">
        <div>
          <Heading
            level={3}
            className="text-2xl font-semibold text-cyan-400 mb-4"
          >
            Server Whitelisting Clients
          </Heading>
          <p className="text-gray-300 mb-4">
            Restrict which clients can connect to your server.
          </p>

          <div className="space-y-6">
            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-cyan-300 mb-3"
              >
                Whitelist file (authorized_keys format)
              </Heading>
              <CodeBlock language="bash">{`# Create allowed_clients.txt with one public key per line
cat allowed_clients.txt
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF... alice@example.com
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIG... bob@example.com

# Server only accepts whitelisted clients
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys allowed_clients.txt`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-purple-300 mb-3"
              >
                Whitelist GitHub user's GPG keys
              </Heading>
              <CodeBlock language="bash">{`# Fetch all GPG keys from GitHub user
ascii-chat server --key gpg:MYKEYID --client-keys github:ethernetdan.gpg

# Client must authenticate with their GPG key
ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F6`}</CodeBlock>
            </div>
          </div>
        </div>

        <div>
          <Heading
            level={3}
            className="text-2xl font-semibold text-teal-400 mb-4"
          >
            Client Whitelisting Servers
          </Heading>
          <p className="text-gray-300 mb-4">
            Only connect to servers with known, trusted keys.
          </p>

          <div className="space-y-6">
            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-teal-300 mb-3"
              >
                Verify with local key file
              </Heading>
              <CodeBlock language="bash">{`# Client verifies server matches this exact key
ascii-chat happy-sunset-ocean --server-key ~/.ssh/known_server.pub`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-pink-300 mb-3"
              >
                Verify with GitHub SSH keys
              </Heading>
              <CodeBlock language="bash">{`# Fetches server's SSH keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:agentwaj.keys`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-cyan-300 mb-3"
              >
                Verify with GitHub GPG keys
              </Heading>
              <CodeBlock language="bash">{`# Fetches server's GPG keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg`}</CodeBlock>
            </div>

            <div>
              <Heading
                level={4}
                className="text-lg font-semibold text-purple-300 mb-3"
              >
                Verify with GPG key ID
              </Heading>
              <CodeBlock language="bash">{`# Verify against specific GPG key from keyring
ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}</CodeBlock>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
