import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function DiscoverySecuritySection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
      >
        🔍 Discovery Service Security
      </Heading>

      <p className="text-gray-300 mb-6">
        When using the ACDS discovery service for P2P session discovery, there
        are additional security flags that control trust between clients,
        servers, and the discovery service itself.
      </p>

      <div className="space-y-8">
        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Verifying the discovery server (--discovery-service-key)
          </Heading>
          <p className="text-gray-300 mb-4">
            Pin the discovery server's public key so you know you're talking to
            the real ACDS instance and not an impersonator. Accepts the same key
            formats as{" "}
            <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
              --key
            </code>
            .
          </p>
          <CodeBlock language="bash">{`# Pin discovery server key from a local file
ascii-chat --discovery-service-key ~/.ssh/acds_server.pub happy-sunset-ocean

# Pin from GitHub SSH keys
ascii-chat --discovery-service-key github:acds-operator happy-sunset-ocean

# Pin from a URL
ascii-chat --discovery-service-key https://acds.example.com/pubkey.pub happy-sunset-ocean

# Server registering with ACDS, pinning the discovery key
ascii-chat server --discovery-service --discovery-service-key github:acds-operator`}</CodeBlock>

          <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4 mt-4">
            <p className="text-gray-300 text-sm">
              <strong className="text-cyan-300">Gotcha:</strong> This flag is
              available in <strong className="text-cyan-300">server</strong>,{" "}
              <strong className="text-purple-300">client</strong>, and{" "}
              <strong className="text-teal-300">discovery</strong> modes — but
              not discovery-service mode (the ACDS server doesn't verify
              itself).
            </p>
          </div>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-yellow-300 mb-3"
          >
            Skipping discovery verification (--discovery-insecure)
          </Heading>
          <p className="text-gray-300 mb-4">
            If you don't have the discovery server's public key and want to
            connect anyway, you can skip verification. This makes you vulnerable
            to MITM attacks on the discovery channel.
          </p>
          <CodeBlock language="bash">{`# Skip discovery server key verification (client/discovery only)
ascii-chat --discovery-insecure happy-sunset-ocean`}</CodeBlock>

          <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-4 mt-4">
            <p className="text-yellow-200 text-sm">
              <strong>Gotcha:</strong>{" "}
              <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
                --discovery-insecure
              </code>{" "}
              only affects the discovery channel. The actual peer-to-peer
              connection still uses normal encryption and authentication. An
              attacker on the discovery channel could redirect you to a
              different server, but they can't decrypt your actual chat session
              (unless you also skip server key verification).
            </p>
          </div>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Requiring identity from peers (--require-server-identity,
            --require-client-identity)
          </Heading>
          <p className="text-gray-300 mb-4">
            These flags are for{" "}
            <strong className="text-pink-300">
              discovery-service operators
            </strong>{" "}
            running their own ACDS instance. They force participants to
            cryptographically prove their identity before the discovery service
            will let them register or discover sessions.
          </p>
          <CodeBlock language="bash">{`# Run a strict ACDS: both servers and clients must prove identity
ascii-chat discovery-service \\
  --require-server-identity \\
  --require-client-identity \\
  --port 27225

# Only require server identity (clients can be anonymous)
ascii-chat discovery-service --require-server-identity

# Only require client identity (servers can be anonymous)
ascii-chat discovery-service --require-client-identity`}</CodeBlock>

          <div className="bg-purple-900/20 border border-purple-700/50 rounded-lg p-4 mt-4">
            <p className="text-gray-300 text-sm">
              <strong className="text-purple-300">Gotcha:</strong> These flags
              are{" "}
              <strong className="text-pink-300">
                discovery-service mode only
              </strong>
              . They have no effect on server, client, or discovery modes.
              Clients and servers connecting to a strict ACDS must have a{" "}
              <code className="text-pink-400 bg-gray-950 px-1.5 py-0.5 rounded">
                --key
              </code>{" "}
              configured or the ACDS will reject them.
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}
