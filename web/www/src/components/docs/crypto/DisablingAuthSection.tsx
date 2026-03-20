import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function DisablingAuthSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-red-400 mb-6 border-b border-red-900/50 pb-2"
      >
        🚫 Disabling Authentication
      </Heading>

      <p className="text-gray-300 mb-6">
        The{" "}
        <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
          --no-auth
        </code>{" "}
        flag disables the authentication layer. On its own, this removes
        identity verification but keeps encryption active (ephemeral DH). When
        combined with{" "}
        <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
          --no-encrypt
        </code>
        , it bypasses the ACIP crypto handshake entirely.
      </p>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-yellow-300 mb-3"
          >
            --no-auth alone (encrypted but anonymous)
          </Heading>
          <p className="text-gray-300 mb-4">
            Traffic is still encrypted with ephemeral keys, but neither side
            proves its identity. No{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --key
            </code>
            ,{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --password
            </code>
            ,{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --server-key
            </code>
            , or{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --client-keys
            </code>{" "}
            checking is performed.
          </p>
          <CodeBlock language="bash">{`# Server: encrypted but no identity verification
ascii-chat server --no-auth

# Client: encrypted but no identity verification
ascii-chat client 192.168.1.100 --no-auth`}</CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-red-300 mb-3"
          >
            --no-auth + --no-encrypt (bypass ACIP entirely)
          </Heading>
          <p className="text-gray-300 mb-4">
            Skips the entire cryptographic handshake. Raw, unencrypted,
            unauthenticated packets on the wire. Useful for local debugging or
            benchmarking without crypto overhead.
          </p>
          <CodeBlock language="bash">{`# Server: no crypto at all
ascii-chat server --no-auth --no-encrypt

# Client: no crypto at all
ascii-chat client 127.0.0.1 --no-auth --no-encrypt`}</CodeBlock>
        </div>
      </div>

      <div className="bg-red-900/20 border border-red-700/50 rounded-lg p-6 mt-6">
        <p className="text-red-200 mb-3">
          <strong>Both sides must agree.</strong> If the server uses{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-auth
          </code>{" "}
          but the client doesn't (or vice versa), the handshake will fail
          because the two sides are negotiating different protocol levels.
        </p>
        <p className="text-gray-300 text-sm">
          The same applies to{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-encrypt
          </code>
          : one encrypted side and one unencrypted side will not connect.
        </p>
      </div>

      <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4 mt-4">
        <p className="text-gray-300 text-sm">
          <strong className="text-cyan-300">Gotcha:</strong>{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-auth
          </code>{" "}
          and{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-encrypt
          </code>{" "}
          are independent flags. You can use{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-auth
          </code>{" "}
          without{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-encrypt
          </code>{" "}
          (anonymous but encrypted), or{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-encrypt
          </code>{" "}
          without{" "}
          <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
            --no-auth
          </code>{" "}
          (unencrypted but still authenticated). They compose independently.
        </p>
      </div>
    </section>
  );
}
