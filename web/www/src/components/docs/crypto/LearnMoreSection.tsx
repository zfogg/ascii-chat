import { Heading } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";
import { SITES } from "@ascii-chat/shared/utils";

export default function LearnMoreSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
      >
        📚 Learn More
      </Heading>

      <div className="grid md:grid-cols-2 gap-4">
        <TrackedLink
          href={SITES.DISCOVERY}
          label="Crypto - ACDS Server"
          target="_blank"
          rel="noopener noreferrer"
          className="bg-gray-900/50 border border-pink-900/50 rounded-lg p-4 hover:border-pink-500/50 transition-colors"
        >
          <Heading level={3} className="text-pink-300 font-semibold mb-1">
            🔍 Discovery Server
          </Heading>
          <p className="text-gray-400 text-sm">
            Public keys and discovery service details
          </p>
        </TrackedLink>

        <TrackedLink
          href="https://zfogg.github.io/ascii-chat/group__handshake.html"
          label="Crypto - Handshake Protocol"
          target="_blank"
          rel="noopener noreferrer"
          className="bg-gray-900/50 border border-cyan-900/50 rounded-lg p-4 hover:border-cyan-500/50 transition-colors"
        >
          <Heading level={3} className="text-cyan-300 font-semibold mb-1">
            🔐 Handshake Protocol
          </Heading>
          <p className="text-gray-400 text-sm">
            Detailed protocol documentation
          </p>
        </TrackedLink>

        <TrackedLink
          href="https://libsodium.gitbook.io/doc/"
          label="Crypto - libsodium Docs"
          target="_blank"
          rel="noopener noreferrer"
          className="bg-gray-900/50 border border-purple-900/50 rounded-lg p-4 hover:border-purple-500/50 transition-colors"
        >
          <Heading level={3} className="text-purple-300 font-semibold mb-1">
            📖 libsodium Docs
          </Heading>
          <p className="text-gray-400 text-sm">
            Cryptography library reference
          </p>
        </TrackedLink>
      </div>
    </section>
  );
}
