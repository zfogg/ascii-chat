import { Heading } from "@ascii-chat/shared/components";

export default function HowItWorksSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
      >
        🔒 How It Works
      </Heading>

      <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
        <p className="text-gray-300 mb-4">
          ascii-chat uses <strong className="text-purple-300">libsodium</strong>{" "}
          for cryptography—the same library that powers Signal, WireGuard, and
          Zcash.
        </p>
        <p className="text-gray-300">
          Every connection performs a Diffie-Hellman key exchange to establish a
          secure tunnel. Your video and audio are encrypted with{" "}
          <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
            XSalsa20-Poly1305
          </code>{" "}
          before leaving your machine.{" "}
          <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
            Ed25519
          </code>{" "}
          signatures verify peer identity,{" "}
          <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
            X25519
          </code>{" "}
          <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
            ECDH
          </code>{" "}
          provides perfect forward secrecy, and the{" "}
          <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
            XSalsa20-Poly1305
          </code>{" "}
          <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
            AEAD
          </code>{" "}
          cipher protects all data in transit.
        </p>
      </div>
    </section>
  );
}
