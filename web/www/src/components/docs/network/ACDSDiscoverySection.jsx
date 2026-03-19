import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ACDSDiscoverySection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🔍 ACDS Discovery Service
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          What is ACDS?
        </Heading>
        <p className="docs-paragraph">
          ascii-chat Discovery Service (ACDS) is a rendezvous server that
          helps clients find each other using memorable session strings.
          ACDS attempts NAT traversal (UPnP, STUN, TURN) and provides
          connection metadata. Crucially:{" "}
          <strong>ACDS never sees media</strong>— only connection
          information.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Session Strings
        </Heading>
        <p className="docs-paragraph">
          Format: <code className="code-inline">adjective-noun-noun</code>
        </p>
        <CodeBlock language="bash">
          {
            "Examples:\n├─ happy-sunset-ocean\n├─ bright-forest-river\n├─ quick-silver-fox\n└─ silent-morning-sky\n\n16.7 million+ possible combinations\nEasy to speak, remember, type\nExpire when server disconnects"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Discovery Mode: Server Registration
        </Heading>
        <p className="docs-paragraph">
          Start a server to create a session string and register with
          ACDS. The session string is auto-generated and printed when the
          server starts:
        </p>
        <CodeBlock language="bash">
          {
            "# Default: Start server and register with official ACDS\nascii-chat\n\n# With authentication (password + SSH key)\nascii-chat --password 'secret123' --key ~/.ssh/id_ed25519\n\n# With port forwarding for direct TCP\nascii-chat --port-forwarding\n\n# With custom ACDS server\nascii-chat --discovery-service discovery.example.com:27225"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Discovery Mode: Client Connection
        </Heading>
        <p className="docs-paragraph">
          Connect to a running server using its session string (no IP
          needed):
        </p>
        <CodeBlock language="bash">
          {
            "# Connect using session string\nascii-chat happy-sunset-ocean\n\n# With authentication\nascii-chat happy-sunset-ocean --password 'secret123' --server-key github:username\n\n# Force TURN relay (very restrictive networks)\nascii-chat happy-sunset-ocean --webrtc-skip-stun\n\n# Custom discovery server\nascii-chat happy-sunset-ocean --discovery-service discovery.example.com"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
