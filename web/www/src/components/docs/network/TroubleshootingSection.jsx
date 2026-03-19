import { Heading } from "@ascii-chat/shared/components";

export default function TroubleshootingSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-yellow-400">
        🔧 Troubleshooting
      </Heading>

      <div className="space-y-3">
        <div className="card-standard accent-red">
          <Heading level={4} className="text-red-300 font-semibold mb-2">
            Connection Refused
          </Heading>
          <p className="text-gray-300 text-sm">
            Ensure server is running and port is accessible. Check firewall
            settings. If behind corporate proxy, enable TURN.
          </p>
        </div>
        <div className="card-standard accent-yellow">
          <Heading level={4} className="text-yellow-300 font-semibold mb-2">
            Authentication Failed
          </Heading>
          <p className="text-gray-300 text-sm">
            Verify password or server key matches. For SSH key auth, ensure
            public key is in server's authorized_keys or GitHub account is
            correct.
          </p>
        </div>
        <div className="card-standard accent-cyan">
          <Heading level={4} className="text-cyan-300 font-semibold mb-2">
            Slow Connection
          </Heading>
          <p className="text-gray-300 text-sm">
            Reduce frame rate: <code className="code-inline">--fps 20</code>.
            Disable audio:
            <code className="code-inline">--no-audio</code>. Enable lower color
            mode: <code className="code-inline">--color-mode 16</code>.
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading level={4} className="text-purple-300 font-semibold mb-2">
            TURN Relay Only
          </Heading>
          <p className="text-gray-300 text-sm">
            If connection quality is poor, you're likely on TURN relay (highest
            latency). Typical on corporate networks. Try: enable UPnP on your
            router, or use the same physical network.
          </p>
        </div>
      </div>
    </section>
  );
}
