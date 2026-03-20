import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import TrackedLink from "../TrackedLink";

export default function DocumentationSection() {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2"
      >
        📚 Documentation
      </Heading>

      <div className="grid sm:grid-cols-2 gap-4">
        <TrackedLink
          to="/docs"
          label="Home - Documentation"
          className="bg-gray-900/50 border border-teal-900/50 rounded-lg p-4 hover:border-teal-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-teal-300 font-semibold mb-1"
            anchorLink={false}
          >
            📚 Documentation
          </Heading>
          <p className="text-gray-400 text-sm">
            Configuration, hardware, terminal, snapshot, network, media
          </p>
        </TrackedLink>

        <TrackedLink
          to="/man1"
          label="Home - Docs Man Page (1)"
          className="bg-gray-900/50 border border-cyan-900/50 rounded-lg p-4 hover:border-cyan-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-cyan-300 font-semibold mb-1"
            anchorLink={false}
          >
            📖 ascii-chat(1)
          </Heading>
          <p className="text-gray-400 text-sm">
            Complete command-line man(1) page reference
          </p>
        </TrackedLink>

        <TrackedLink
          to="/docs/crypto"
          label="Home - Docs Cryptography"
          className="bg-gray-900/50 border border-purple-900/50 rounded-lg p-4 hover:border-purple-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-purple-300 font-semibold mb-1"
            anchorLink={false}
          >
            🔐 Cryptography
          </Heading>
          <p className="text-gray-400 text-sm">
            Encryption, keys, and authentication
          </p>
        </TrackedLink>

        <TrackedLink
          to="/man3"
          label="Home - Docs Man Page (3)"
          className="bg-gray-900/50 border border-pink-900/50 rounded-lg p-4 hover:border-pink-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-pink-300 font-semibold mb-1"
            anchorLink={false}
          >
            📚 ascii-chat-*(3)
          </Heading>
          <p className="text-gray-400 text-sm">
            Documentation for the source code as man(3) pages
          </p>
        </TrackedLink>

        <TrackedLink
          to="/man1#OPTIONS"
          label="Home - Man1 OPTIONS"
          className="bg-gray-900/50 border border-green-900/50 rounded-lg p-4 hover:border-green-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-green-300 font-semibold mb-1"
            anchorLink={false}
          >
            🌍 Command Line --options
          </Heading>
          <p className="text-gray-400 text-sm">
            See man(1) page OPTIONS section
          </p>
        </TrackedLink>

        <TrackedLink
          href={SITES.DISCOVERY}
          label="Home - Docs ACDS"
          target="_blank"
          rel="noopener noreferrer"
          className="bg-gray-900/50 border border-teal-900/50 rounded-lg p-4 hover:border-teal-500/50 transition-colors"
        >
          <Heading
            level={3}
            className="text-teal-300 font-semibold mb-1"
            anchorLink={false}
          >
            🔍 Discovery Service
          </Heading>
          <p className="text-gray-400 text-sm">ACDS public keys and details</p>
        </TrackedLink>
      </div>
    </section>
  );
}
