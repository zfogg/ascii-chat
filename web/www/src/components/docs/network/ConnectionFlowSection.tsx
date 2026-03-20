import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function ConnectionFlowSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-yellow-400">
        🔗 Connection Flow
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          TCP Handshake & Crypto Setup
        </Heading>
        <CodeBlock language="bash">
          {
            "UNENCRYPTED HANDSHAKE:\n1. TCP handshake\n2. PROTOCOL_VERSION negotiation (version check)\n3. CRYPTO_CLIENT_HELLO (client key fingerprint)\n4. CRYPTO_CAPABILITIES exchange (algorithms)\n5. CRYPTO_KEY_EXCHANGE_INIT (server's ephemeral key)\n6. CRYPTO_KEY_EXCHANGE_RESP (client's ephemeral key)\n7. CRYPTO_AUTH_CHALLENGE (server nonce)\n8. CRYPTO_AUTH_RESPONSE (client signature)\n9. CRYPTO_SERVER_AUTH_RESP (server signature)\n10. CRYPTO_HANDSHAKE_COMPLETE (keys established)\n\nENCRYPTED SESSION:\n11. CLIENT_CAPABILITIES (terminal dims, color support) ← ENCRYPTED\n12. Server responds with session state ← ENCRYPTED\n13. Media frames begin ← ENCRYPTED\n\nKey Property: TCP guarantees packet order,\nACIP relies on this for frame integrity"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Perfect Forward Secrecy
        </Heading>
        <p className="docs-paragraph">
          Each session uses ephemeral X25519 keys that are discarded after use.
          Even if long-term keys are compromised, past sessions cannot be
          decrypted. Periodic key rotation for long-lived connections.
        </p>
      </div>
    </section>
  );
}
