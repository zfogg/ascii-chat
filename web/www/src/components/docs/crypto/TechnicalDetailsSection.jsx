import { Heading } from "@ascii-chat/shared/components";

export default function TechnicalDetailsSection() {
  return (
    <section className="mb-16">
      <Heading
        level={2}
        className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
      >
        🔬 Technical Details
      </Heading>

      <div className="space-y-6">
        <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
          <Heading level={3} className="text-cyan-300 font-semibold mb-3">
            Handshake Protocol
          </Heading>
          <ol className="list-decimal list-inside space-y-2 text-gray-300">
            <li>
              Client and server exchange Ed25519 public keys (optional
              identity verification)
            </li>
            <li>Both sides generate ephemeral X25519 keypairs</li>
            <li>Diffie-Hellman key exchange computes shared secret</li>
            <li>
              Shared secret derives session keys for XSalsa20-Poly1305
              encryption
            </li>
            <li>All packets encrypted with session keys</li>
          </ol>
        </div>

        <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
          <Heading
            level={3}
            className="text-purple-300 font-semibold mb-3"
          >
            Encryption Algorithms
          </Heading>
          <ul className="space-y-2 text-gray-300">
            <li>
              <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
                Ed25519
              </code>{" "}
              - Identity signatures (64 bytes)
            </li>
            <li>
              <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
                X25519
              </code>{" "}
              - ECDH key exchange (32 bytes)
            </li>
            <li>
              <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                XSalsa20-Poly1305
              </code>{" "}
              - AEAD cipher (encrypt + authenticate)
            </li>
            <li>
              <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                Blowfish
              </code>{" "}
              - Block cipher (SSH private key encryption)
            </li>
            <li>
              <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
                BLAKE2b
              </code>{" "}
              - Key derivation function
            </li>
            <li>
              <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
                Argon2
              </code>{" "}
              - Password hashing (memory-hard KDF)
            </li>
          </ul>
        </div>

        <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
          <Heading level={3} className="text-teal-300 font-semibold mb-3">
            Perfect Forward Secrecy
          </Heading>
          <p className="text-gray-300">
            Each connection generates new ephemeral keys. Even if your
            long-term SSH/GPG key is compromised, past sessions cannot be
            decrypted. Keys are never stored—only used for the duration of
            the connection.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
          <Heading level={3} className="text-pink-300 font-semibold mb-3">
            Session Rekeying
          </Heading>
          <p className="text-gray-300 mb-3">
            Automatic key rotation limits exposure if session keys are
            compromised. Rekeys occur after{" "}
            <strong className="text-pink-400">1 hour</strong> or{" "}
            <strong className="text-pink-400">1 million packets</strong>,
            whichever comes first.
          </p>
          <p className="text-gray-300 text-sm">
            The rekey protocol is transparent: both sides generate new
            ephemeral keys, perform a new DH exchange, and switch to new
            session keys without interrupting the connection. Old keys
            remain active until the new handshake completes successfully.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
          <Heading level={3} className="text-cyan-300 font-semibold mb-3">
            Nonce Construction
          </Heading>
          <p className="text-gray-300 mb-2">
            XSalsa20-Poly1305 uses 24-byte nonces constructed as{" "}
            <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
              session_id || counter
            </code>
            :
          </p>
          <ul className="list-disc list-inside space-y-1 text-gray-300 text-sm">
            <li>
              First 16 bytes: Random session ID (generated at connection
              start)
            </li>
            <li>
              Remaining 8 bytes: Monotonic packet counter (increments per
              packet)
            </li>
          </ul>
          <p className="text-gray-300 mt-2 text-sm">
            This prevents both within-session and cross-session replay
            attacks. Even if an attacker captures encrypted packets, they
            can't be replayed because the nonce is unique per packet and
            per session.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
          <Heading
            level={3}
            className="text-purple-300 font-semibold mb-3"
          >
            Constant-Time Operations
          </Heading>
          <p className="text-gray-300 mb-2">
            All cryptographic comparisons use constant-time algorithms
            (via{" "}
            <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
              sodium_memcmp
            </code>
            ) to prevent timing attacks:
          </p>
          <ul className="list-disc list-inside space-y-1 text-gray-300 text-sm">
            <li>HMAC verification (password authentication)</li>
            <li>MAC verification (packet authentication)</li>
            <li>Key comparisons (identity verification)</li>
          </ul>
          <p className="text-gray-300 mt-2 text-sm">
            Constant-time comparison ensures the time taken is independent
            of where the first difference occurs, preventing attackers
            from learning partial key/MAC values through timing
            side-channels.
          </p>
        </div>
      </div>
    </section>
  );
}
