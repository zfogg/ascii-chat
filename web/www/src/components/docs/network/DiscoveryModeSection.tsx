import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";

export default function DiscoveryModeSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🚀 Discovery Mode (WebRTC P2P)
      </Heading>

      <p className="docs-paragraph">
        Discovery Mode is the default mode for ascii-chat, enabling zero-config
        P2P video calls using memorable session strings (e.g.,{" "}
        <code className="code-inline">purple-mountain-lake</code>
        ). No port forwarding, no technical knowledge required. When you run the
        server without specifying a connection mode, it automatically generates
        a session string and registers with the discovery service. Clients
        connect using that session string. The official ascii-chat
        discovery-service is hosted at{" "}
        <code className="code-inline">discovery-service.ascii-chat.com</code>,
        with keys available at{" "}
        <TrackedLink
          to="https://discover.ascii-chat.com"
          label="discover.ascii-chat.com"
          className="text-cyan-400 hover:text-cyan-300 transition-colors"
        >
          discover.ascii-chat.com
        </TrackedLink>
        . The ascii-chat client downloads these keys over HTTPS, trusts them,
        and uses this ACDS server by default.
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Phase 1: Instant Host Election (100ms)
        </Heading>
        <p className="docs-paragraph">
          Two participants exchange NAT information and instantly elect a host.
          No bandwidth test blocking media startup.
        </p>
        <div className="space-y-3">
          <div className="card-standard accent-teal">
            <Heading level={4} className="text-teal-300 font-semibold mb-2">
              NAT Priority Tiers (Best to Worst)
            </Heading>
            <p className="text-gray-400 text-sm">
              1. Localhost/LAN (same subnet, ~1ms latency)
              <br />
              2. Public IP (direct connection, ~10-50ms)
              <br />
              3. UPnP/NAT-PMP (port mapping, ~20-100ms)
              <br />
              4. STUN hole-punch (ICE connectivity, ~50-200ms)
              <br />
              5. TURN relay (always works, ~100-300ms)
            </p>
          </div>
          <p className="text-gray-400 text-sm">
            <strong>Bandwidth Override:</strong> If one participant has 10x+
            bandwidth, it becomes host regardless of NAT tier.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Phase 2: Media Starts (500ms total)
        </Heading>
        <p className="docs-paragraph">
          WebRTC DataChannel established. All packets (control + media) flow
          through single DataChannel. Host begins rendering, participants begin
          capturing. No connection reset on migration.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Phase 3: Background Quality Measurement (30-60s)
        </Heading>
        <p className="docs-paragraph">
          Bandwidth measured from real frames, not synthetic tests:
        </p>
        <CodeBlock language="bash">
          {
            "Metrics collected:\n├─ Upload Kbps (frame sizes + frame loss)\n├─ RTT (round-trip time via frame timestamps)\n├─ Jitter (RTT variance)\n└─ Packet loss %\n\nScoring: score = (upload_kbps/10) + (100-loss%) + (100-rtt_ms)\nNo delay to media start — measurement is async"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Phase 4: Optional Host Migration
        </Heading>
        <p className="docs-paragraph">
          If a participant scores 20%+ higher quality, host migration happens
          transparently over the same DataChannel. Media resumes within ~100ms.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-red-300 mb-3">
          Instant Failover
        </Heading>
        <p className="docs-paragraph">
          Host broadcasts backup address every 30-60 seconds. If host dies:
        </p>
        <ul className="space-y-1 text-gray-300 text-sm ml-4 list-disc">
          <li>Backup participant becomes host immediately</li>
          <li>Other participants reconnect to stored backup address</li>
          <li>No ACDS query needed</li>
          <li>Recovery: ~300-500ms</li>
          <li>Media resumes automatically without interruption</li>
        </ul>
      </div>
    </section>
  );
}
