import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function NATTraversalSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-orange-400">
        🌍 NAT Traversal
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          UPnP & NAT-PMP
        </Heading>
        <p className="docs-paragraph">
          Works on ~70% of home routers. Automatic port forwarding enables
          direct TCP connections (lowest latency).
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          WebRTC STUN/TURN
        </Heading>
        <p className="docs-paragraph">
          <strong>STUN:</strong> Discovers public IP:port mapping
          <br />
          <strong>ICE:</strong> Negotiates P2P connections through NAT
          <br />
          <strong>TURN:</strong> Relay fallback (works in 99% of networks,
          slightly higher latency)
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          3-Stage Connection Fallback
        </Heading>
        <CodeBlock language="bash">
          {
            "1. Direct TCP         → 3s timeout  (if server has public IP)\n2. WebRTC + STUN      → 8s timeout  (NAT hole-punching)\n3. WebRTC + TURN      → 15s timeout (relay, always works)\n\nTotal time to first connection: up to 26 seconds worst case\nTypical home networks: 1-2 seconds (direct or UPnP)"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
