import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function GotchasSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-yellow-400 mb-6 border-b border-yellow-900/50 pb-2"
      >
        ⚡ Common Gotchas
      </Heading>

      <div className="space-y-4">
        <div className="bg-gray-900/50 border border-yellow-900/30 rounded-lg p-6">
          <Heading level={3} className="text-yellow-300 font-semibold mb-3">
            Both sides must match
          </Heading>
          <p className="text-gray-300 text-sm mb-3">
            Encryption and authentication settings must be the same on both
            sides. If the server runs with{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --no-encrypt
            </code>{" "}
            but the client doesn't, the handshake fails. Same for{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --no-auth
            </code>{" "}
            and{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --password
            </code>
            .
          </p>
          <CodeBlock language="bash">{`# This will FAIL — mismatched encryption settings
ascii-chat server --no-encrypt
ascii-chat client 192.168.1.100          # client expects encryption

# This works — both sides agree
ascii-chat server --no-encrypt
ascii-chat client 192.168.1.100 --no-encrypt`}</CodeBlock>
        </div>

        <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
          <Heading level={3} className="text-cyan-300 font-semibold mb-3">
            --server-key is client-only, --client-keys is server-only
          </Heading>
          <p className="text-gray-300 text-sm">
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --server-key
            </code>{" "}
            only works in client and discovery modes — it's how a client
            verifies the server's identity.{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --client-keys
            </code>{" "}
            only works in server, discovery, and discovery-service modes — it's
            how a server restricts which clients can connect. Using them in the
            wrong mode has no effect.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
          <Heading level={3} className="text-purple-300 font-semibold mb-3">
            Password length requirements
          </Heading>
          <p className="text-gray-300 text-sm">
            Passwords must be 8–256 characters. Shorter passwords are rejected
            at startup. The password is processed through Argon2 key derivation,
            so the actual key material is always strong regardless of password
            entropy — but short passwords are still vulnerable to brute-force on
            the Argon2 output.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
          <Heading level={3} className="text-teal-300 font-semibold mb-3">
            Key format: Ed25519 only
          </Heading>
          <p className="text-gray-300 text-sm mb-3">
            ascii-chat only supports Ed25519 keys (SSH or GPG). RSA, ECDSA, and
            DSA keys are not supported. If you pass an unsupported key type,
            you'll get an error at startup.
          </p>
          <CodeBlock language="bash">{`# Generate a compatible key if you don't have one
ssh-keygen -t ed25519 -f ~/.ssh/id_ed25519_ascii_chat`}</CodeBlock>
        </div>

        <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
          <Heading level={3} className="text-pink-300 font-semibold mb-3">
            --discovery-insecure only affects the discovery channel
          </Heading>
          <p className="text-gray-300 text-sm">
            Skipping discovery server verification does not weaken the actual
            peer-to-peer connection. The P2P link still negotiates its own
            encrypted session. However, an attacker controlling the discovery
            channel could redirect you to a malicious server — so pair{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --discovery-insecure
            </code>{" "}
            with{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --server-key
            </code>{" "}
            for best protection.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-green-900/30 rounded-lg p-6">
          <Heading level={3} className="text-green-300 font-semibold mb-3">
            --require-server-identity and --require-client-identity are DS-only
          </Heading>
          <p className="text-gray-300 text-sm">
            These flags only apply to the{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              discovery-service
            </code>{" "}
            mode. They tell the ACDS server to reject participants who don't
            provide a signed Ed25519 identity. If you set these, clients and
            servers connecting to your ACDS must use{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --key
            </code>
            .
          </p>
        </div>
      </div>
    </section>
  );
}
