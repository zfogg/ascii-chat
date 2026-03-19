import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function PasswordEncryptionSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2"
      >
        🔐 Password-Based Encryption
      </Heading>

      <p className="text-gray-300 mb-6">
        Simple password encryption for quick sessions. Combine with key
        authentication for defense-in-depth.
      </p>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Password-only
          </Heading>
          <CodeBlock language="bash">{`# Server sets password
ascii-chat server --password "correct-horse-battery-staple"

# Client must know the password
ascii-chat client 192.168.1.100 --password "correct-horse-battery-staple"`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Key + Password (maximum security)
          </Heading>
          <CodeBlock language="bash">{`# Both SSH key and password required
ascii-chat server --key ~/.ssh/id_ed25519 --password "extra-secret"

# Client needs both to connect
ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519 --password "extra-secret"`}</CodeBlock>
        </div>
      </div>
    </section>
  );
}
