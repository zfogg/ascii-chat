import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function DisablingEncryptionSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2"
      >
        🚫 Disabling Encryption
      </Heading>

      <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-6 mb-6">
        <p className="text-yellow-200">
          <strong>⚠️ Warning:</strong> Only disable encryption for local
          testing on trusted networks. Your video and audio will be sent
          unencrypted over the network.
        </p>
      </div>

      <CodeBlock language="bash">{`# Server with encryption disabled
ascii-chat server --no-encrypt

# Client must also disable encryption
ascii-chat client 127.0.0.1 --no-encrypt`}</CodeBlock>

      <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4 mt-6">
        <p className="text-gray-300 text-sm">
          <strong className="text-cyan-300">Note:</strong> Both client and
          server must use{" "}
          <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
            --no-encrypt
          </code>{" "}
          for unencrypted mode to work. If only one side disables
          encryption, the connection will fail during the handshake.
        </p>
      </div>
    </section>
  );
}
