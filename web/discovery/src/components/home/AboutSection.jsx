import { Heading, Link } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

export default function AboutSection({ sessionStrings, handleLinkClick }) {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        📋 About ACDS
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        The <strong>ascii-chat Discovery Service (ACDS)</strong> is a core
        component of{" "}
        <Link
          href={SITES.MAIN}
          onClick={() =>
            handleLinkClick(SITES.MAIN, "ascii-chat website (about)")
          }
        >
          ascii-chat
        </Link>
        , a real-time terminal-based video chat application. ACDS enables
        session discovery using memorable three-word strings like{" "}
        <code className="bg-gray-800 px-1 rounded">{sessionStrings[0]}</code>{" "}
        instead of IP addresses. It provides NAT traversal, WebRTC signaling,
        and peer-to-peer connection establishment.
      </p>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        <strong>Privacy-first:</strong> ACDS only exchanges connection metadata.
        Your audio and video never pass through our servers. All media flows
        peer-to-peer with end-to-end encryption by default.
      </p>

      <Heading
        level={3}
        className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
      >
        🏗️ Official ACDS Infrastructure
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        The official ACDS deployment consists of two components:
      </p>
      <ul className="leading-relaxed ml-0 pl-4 space-y-2">
        <li>
          <strong>This website</strong> (
          <code className="bg-gray-800 px-1 rounded">
            {window.location.hostname}
          </code>
          ) - Serves public keys over HTTPS
        </li>
        <li>
          <strong>Official ACDS server instance</strong> (
          <code className="bg-gray-800 px-1 rounded">
            tcp://discovery-service.ascii-chat.com:27225
          </code>
          ) - Handles session management (TCP)
        </li>
      </ul>
    </section>
  );
}
