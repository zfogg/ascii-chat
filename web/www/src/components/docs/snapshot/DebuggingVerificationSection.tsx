import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function DebuggingVerificationSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        🔍 Debugging & Network Verification
      </Heading>

      <p className="docs-paragraph">
        Snapshot mode is perfect for verifying your setup. A successful snapshot
        means the entire pipeline is working:
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          What a Successful Snapshot Verifies
        </Heading>
        <ul className="space-y-1 text-gray-300 text-sm">
          <li>
            <span className="text-cyan-300">✅ Webcam Access</span> — Webcam is
            working and accessible to ascii-chat
          </li>
          <li>
            <span className="text-teal-300">✅ Video Encoding</span> — Image
            capture, scaling, and ASCII conversion all work correctly
          </li>
          <li>
            <span className="text-purple-300">✅ Terminal Rendering</span> —
            Terminal capabilities detected and ANSI codes generated correctly
          </li>
          <li>
            <span className="text-pink-300">✅ Network (Client Mode)</span> —
            Connection to server succeeded and one frame was received
          </li>
          <li>
            <span className="text-green-300">
              ✅ Crypto Handshake (Client Mode)
            </span>{" "}
            — Encryption negotiation succeeded, keys were established
          </li>
          <li>
            <span className="text-cyan-300">✅ ACDS (Discovery Mode)</span> —
            Discovery service lookup, NAT traversal, and peer connection all
            worked
          </li>
        </ul>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Verification Examples
        </Heading>
        <CodeBlock language="bash">
          {
            '# Test local webcam only\nascii-chat mirror -S -D 0\necho "Exit code: $?"  # 0 = success\n\n# Test network connection to server\nascii-chat client example.com -S -D 3\necho "Exit code: $?"  # Verifies: network, crypto, frame transmission\n\n# Test with specific encryption\nascii-chat client example.com --key ~/.ssh/id_ed25519 -S -D 3\necho "Exit code: $?"  # Verifies: key auth, handshake, transmission\n\n# Test ACDS discovery and WebRTC\nascii-chat my-session-string -S -D 5\necho "Exit code: $?"  # Verifies: ACDS lookup, NAT traversal, P2P connection'
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Exit Codes for Scripting
        </Heading>
        <CodeBlock language="bash">
          {
            '# Check if snapshot succeeded\nif ascii-chat mirror -S -D 1 > /dev/null 2>&1; then\n  echo "✓ Webcam working"\nelse\n  echo "✗ Webcam failed (exit code: $?)"\nfi\n\n# Verify server connection\nif ascii-chat client example.com:8080 -S -D 3 > /tmp/frame.txt 2>&1; then\n  echo "✓ Connected to server and received frame"\n  cat /tmp/frame.txt | head -20  # Show first 20 lines\nelse\n  echo "✗ Server connection failed (exit code: $?)"\nfi\n\n# Retry logic\nfor attempt in {1..3}; do\n  echo "Attempt $attempt..."\n  if ascii-chat client example.com -S -D 2; then\n    echo "Success!"\n    break\n  fi\n  echo "Failed, retrying..."\n  sleep 2\ndone'
          }
        </CodeBlock>
      </div>
    </section>
  );
}
