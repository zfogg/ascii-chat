import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function TraditionalModeSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        ⚡ Traditional Mode Setup
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Local Network Connection
        </Heading>
        <p className="docs-paragraph">
          Direct P2P connection on same LAN (fastest, ~1-10ms latency):
        </p>
        <CodeBlock language="bash">
          {
            "# Server: Bind to all interfaces (IPv4 + IPv6) on default port\nascii-chat server\n\n# Server: Bind to specific IPv4 and IPv6 interfaces together\nascii-chat server 192.168.1.50 '[2001:db8::1]'\n\n# Client: Connect to server via IPv4\nascii-chat client 192.168.1.50:27224\n\n# Client: Connect to server via IPv6\nascii-chat client '[2001:db8::1]:27224'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Custom Ports & Advanced Options
        </Heading>
        <CodeBlock language="bash">
          {
            "# Server: Listen on custom port (all interfaces)\nascii-chat server --port 8080\n\n# Server: Bind to multiple addresses with custom port\nascii-chat server --port 8080 192.168.1.50 '[2001:db8::1]'\n\n# Client: Connect via IPv4 with custom port\nascii-chat client 192.168.1.50:8080\n\n# Client: Connect via IPv6 with custom port\nascii-chat client '[2001:db8::1]:8080'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          mDNS (Zero-Config LAN Discovery)
        </Heading>
        <p className="docs-paragraph">
          Auto-discover servers on local network without knowing IP addresses:
        </p>
        <CodeBlock language="bash">
          {
            "# Client: Scan for servers on local network via mDNS\nascii-chat client --scan\n\n# Server: Server can be discovered on local network\nascii-chat server\n\n# Client: Discover and list all available servers\nascii-chat client --scan\n\n# Discovery mode also supports mDNS for peer-to-peer discovery\nascii-chat client session-string --scan"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Port Forwarding & External Access
        </Heading>
        <p className="docs-paragraph">
          Enable connections from outside your network:
        </p>
        <CodeBlock language="bash">
          {
            "# Server: Enable automatic UPnP/NAT-PMP port mapping\nascii-chat server --port-forwarding\n\n# Server: Port forwarding with custom port\nascii-chat server --port 8080 --port-forwarding\n\n# Client: Connect via hostname (supports IPv4 and IPv6)\nascii-chat client example.com:27224\n\n# Client: Connect to IPv6 address directly\nascii-chat client '[2001:db8::1]:27224'\n\n# Server with discovery and port forwarding\nascii-chat server --discovery --port-forwarding"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
