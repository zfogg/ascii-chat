import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function GPGKeyAuthSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
      >
        🛡️ GPG Key Authentication
      </Heading>

      <p className="text-gray-300 mb-6">
        GPG Ed25519 keys work via{" "}
        <strong className="text-teal-300">gpg-agent</strong>. No passphrase
        prompts.
      </p>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Using GPG key ID
          </Heading>
          <CodeBlock language="bash">{`# Server with GPG key (short/long/full fingerprint)
ascii-chat server --key gpg:7FE90A79F2E80ED3

# Client connects with their GPG key
ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3`}</CodeBlock>
        </div>
      </div>
    </section>
  );
}
