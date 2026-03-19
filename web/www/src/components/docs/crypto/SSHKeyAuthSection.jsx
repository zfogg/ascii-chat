import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../../TrackedLink";

export default function SSHKeyAuthSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2"
      >
        🔑 SSH Key Authentication
      </Heading>

      <p className="text-gray-300 mb-6">
        Use your existing SSH Ed25519 keys for authentication. ascii-chat reads
        the same keys you use for GitHub, servers, and git. (Configure these in
        your{" "}
        <TrackedLink
          to="/docs/configuration"
          label="ssh key config"
          className="text-cyan-400 hover:text-cyan-300 transition-colors"
        >
          config file
        </TrackedLink>
        )
      </p>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Server with SSH key
          </Heading>
          <CodeBlock language="bash">{`# Server authenticates with its key
ascii-chat server --key ~/.ssh/id_ed25519`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Client connects with their key
          </Heading>
          <CodeBlock language="bash">{`# Client authenticates with their key
ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Encrypted SSH keys
          </Heading>
          <CodeBlock language="bash">{`# Prompts for passphrase or uses ssh-agent
ascii-chat server --key ~/.ssh/id_ed25519_encrypted

# Or set passphrase via environment variable
export ASCII_CHAT_KEY_PASSWORD="my-passphrase"
ascii-chat server --key ~/.ssh/id_ed25519_encrypted`}</CodeBlock>
          <p className="text-gray-400 text-sm mt-3">
            See the{" "}
            <TrackedLink
              to="/man1#ENVIRONMENT"
              label="Crypto - Env Vars Link"
              className="text-green-400 hover:text-green-300 underline"
            >
              Man Page ENVIRONMENT section
            </TrackedLink>{" "}
            for all available configuration options.
          </p>
        </div>
      </div>
    </section>
  );
}
