import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function ServerVerificationSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
      >
        ✅ Server Identity Verification
      </Heading>

      <p className="text-gray-300 mb-6">
        Prevent man-in-the-middle attacks by verifying the server's public key
        before connecting.
      </p>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Verify with local public key file
          </Heading>
          <CodeBlock language="bash">{`# Client verifies server identity
ascii-chat happy-sunset-ocean --server-key ~/.ssh/server1.pub`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-teal-300 mb-3"
          >
            Verify with GitHub/GitLab GPG keys
          </Heading>
          <CodeBlock language="bash">{`# Fetches server's GPG keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:ethernetdan.gpg

# Or from GitLab
ascii-chat happy-sunset-ocean --server-key gitlab:agentwaj.gpg`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Verify with GPG key ID
          </Heading>
          <CodeBlock language="bash">{`# Verify against specific GPG key
ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}</CodeBlock>
        </div>
      </div>
    </section>
  );
}
