import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";

export default function Crypto() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Cryptography", path: "/crypto" },
    ]);
  }, []);
  return (
    <div className="bg-gray-950 text-gray-100 flex flex-col">
      <div className="flex-1 flex flex-col max-w-4xl mx-auto px-4 sm:px-6 py-8 sm:py-12 w-full">
        {/* Header */}
        <header className="mb-12 sm:mb-16">
          <h1 className="text-3xl sm:text-4xl md:text-5xl font-bold mb-4">
            <span className="text-purple-400">🔐</span> Cryptography
          </h1>
          <p className="text-lg sm:text-xl text-gray-300">
            End-to-end encryption with Ed25519 authentication and X25519 key
            exchange
          </p>
        </header>

        {/* ACDS Note */}
        <div className="mb-12 bg-purple-900/20 border border-purple-700/50 rounded-lg p-6">
          <p className="text-gray-300">
            <strong className="text-purple-300">Note:</strong> Looking for
            ascii-chat Discovery Service (ACDS) cryptography details or public
            keys? See the{" "}
            <TrackedLink
              href="https://discovery.ascii-chat.com"
              label="Crypto - ACDS Docs"
              target="_blank"
              rel="noopener noreferrer"
              className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
            >
              ACDS website
            </TrackedLink>{" "}
            for discovery service crypto architecture.
          </p>
        </div>

        {/* Philosophy */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-pink-400 mb-4 sm:mb-6 border-b border-pink-900/50 pb-2">
            🎯 Design Philosophy
          </h2>

          <div className="space-y-6">
            <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-4 sm:p-6">
              <h3 className="text-pink-300 font-semibold mb-3">
                The Core Problem: Trust Without Infrastructure
              </h3>
              <p className="text-gray-300 mb-3">
                HTTPS can rely on Certificate Authorities because there's a
                globally trusted PKI. ascii-chat has no such infrastructure for
                raw TCP connections. Yet users need both{" "}
                <strong className="text-cyan-300">privacy</strong> (against
                passive eavesdropping) and{" "}
                <strong className="text-purple-300">security</strong> (against
                active MITM attacks).
              </p>
              <p className="text-gray-300">
                The solution: a{" "}
                <strong className="text-pink-300">
                  progressive security ladder
                </strong>{" "}
                where users choose their trust level based on their threat
                model.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
              <h3 className="text-purple-300 font-semibold mb-3">
                Five Levels of Security
              </h3>
              <div className="space-y-3 text-gray-300">
                <div className="bg-gray-950/50 border border-cyan-900/30 rounded p-3">
                  <strong className="text-red-400">
                    Level 1: Default Encrypted but Unauthenticated (Vulnerable)
                  </strong>
                  <br />
                  Ephemeral keys, no identity verification. Protects against
                  passive eavesdropping. Works out-of-the-box with zero
                  configuration. Vulnerable to Man-In-The-Middle attacks.
                </div>
                <div className="bg-gray-950/50 border border-purple-900/30 rounded p-3">
                  <strong className="text-purple-400">
                    Level 2: Password Authentication
                  </strong>
                  <br />
                  Both sides prove password knowledge,{" "}
                  <em>bound to the DH shared secret</em>. Prevents MITM attacks
                  even without pre-shared keys.
                </div>
                <div className="bg-gray-950/50 border border-teal-900/30 rounded p-3">
                  <strong className="text-teal-400">
                    Level 3: SSH or GPG Key Pinning
                  </strong>
                  <br />
                  Leverage existing SSH or GPG keys. Server signs ephemeral key;
                  client verifies signature. Known hosts tracking (TOFU model).
                </div>
                <div className="bg-gray-950/50 border border-pink-900/30 rounded p-3">
                  <strong className="text-pink-400">
                    Level 4: Client and/or Server Whitelisting
                  </strong>
                  <br />
                  Servers can enforce authorized client key list. Only
                  pre-approved clients can connect. Clients can authorize the
                  key of the server they're connecting to and will refuse to
                  connect if there's a mismatch.
                </div>
                <div className="bg-gray-950/50 border border-cyan-900/30 rounded p-3">
                  <strong className="text-cyan-400">
                    Level 5: Defense in Depth
                  </strong>
                  <br />
                  Stack multiple methods: password + key pinning + whitelist
                  verification for paranoid security.
                </div>
              </div>
            </div>

            <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
              <h3 className="text-cyan-300 font-semibold mb-3">
                The ACDS Trust Model: Bootstrapping Trust Over HTTPS
              </h3>
              <p className="text-gray-300 mb-3">
                ACDS (ASCII-Chat Discovery Service) solves the trust problem by
                using the{" "}
                <strong className="text-cyan-400">existing HTTPS PKI</strong> to
                bootstrap trust for raw TCP connections:
              </p>
              <ol className="list-decimal list-inside space-y-2 text-gray-300">
                <li>
                  Client downloads ACDS public key from{" "}
                  <TrackedLink
                    href="https://discovery.ascii-chat.com/key.pub"
                    label="Crypto - ACDS SSH Key"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-pink-400 hover:text-pink-300 transition-colors underline"
                  >
                    https://discovery.ascii-chat.com/key.pub
                  </TrackedLink>{" "}
                  (SSH) or{" "}
                  <TrackedLink
                    href="https://discovery.ascii-chat.com/key.gpg"
                    label="Crypto - ACDS GPG Key"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-pink-400 hover:text-pink-300 transition-colors underline"
                  >
                    key.gpg
                  </TrackedLink>{" "}
                  (GPG), verified by system CA certificates via BearSSL
                </li>
                <li>
                  Client connects to{" "}
                  <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
                    discovery-service.ascii-chat.com:27225
                  </code>{" "}
                  (raw TCP) and looks up session
                </li>
                <li>ACDS returns the server's public key for that session</li>
                <li>
                  Client pins that server key and verifies during handshake
                </li>
              </ol>
              <p className="text-gray-300 mt-3">
                Chain of trust: <span className="text-cyan-400">HTTPS CA</span>{" "}
                → <span className="text-purple-400">ACDS key</span> →{" "}
                <span className="text-teal-400">Session lookup</span> →{" "}
                <span className="text-pink-400">Server identity</span>
              </p>
              <p className="text-gray-300 mt-3">
                ACDS only sees connection metadata (session strings, public
                keys, ICE candidates). Your video and audio flow peer-to-peer
                with end-to-end encryption. ACDS never touches your media.
              </p>
              <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4 mt-4">
                <p className="text-gray-300 text-sm">
                  <strong className="text-cyan-300">Note:</strong> For complete
                  ACDS public key information and fingerprints, visit{" "}
                  <TrackedLink
                    href="https://discovery.ascii-chat.com"
                    label="Crypto - ACDS Homepage"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
                  >
                    discovery.ascii-chat.com
                  </TrackedLink>
                  .
                </p>
              </div>
            </div>

            <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
              <h3 className="text-teal-300 font-semibold mb-3">
                Why These Design Choices?
              </h3>
              <ul className="space-y-3 text-gray-300">
                <li>
                  <strong className="text-cyan-400">
                    libsodium over OpenSSL:
                  </strong>{" "}
                  Smaller attack surface, better defaults, fixed key sizes
                  simplify protocol design. No misconfiguration pitfalls.
                </li>
                <li>
                  <strong className="text-purple-400">
                    DH binding for password auth:
                  </strong>{" "}
                  <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                    HMAC(password_key, nonce || shared_secret)
                  </code>{" "}
                  prevents MITM. Attacker can't replay authentication without
                  knowing the DH shared secret.
                </li>
                <li>
                  <strong className="text-teal-400">
                    Separation of identity vs ephemeral keys:
                  </strong>{" "}
                  Ed25519 proves "who you are," X25519 provides "forward
                  secrecy." Even if long-term key is compromised, past sessions
                  remain secret.
                </li>
                <li>
                  <strong className="text-pink-400">Session rekeying:</strong>{" "}
                  Limits impact of key compromise. Automatic rotation after 1
                  hour or 1 million packets—whichever comes first.
                </li>
                <li>
                  <strong className="text-cyan-400">Known hosts (TOFU):</strong>{" "}
                  SSH-style trust model. First connection establishes trust;
                  subsequent connections verify it hasn't changed. Key changes
                  trigger MITM warnings.
                </li>
              </ul>
            </div>

            <div className="bg-purple-900/20 border border-purple-700/50 rounded-lg p-6">
              <h3 className="text-purple-300 font-semibold mb-3">
                💡 Key Insight
              </h3>
              <p className="text-gray-300">
                You can mix and match verification methods. Password-only for
                quick sessions. SSH keys for stronger identity. ACDS for
                zero-config. Stack all three if you want. More verification =
                stronger security, but defaults work without configuration.
              </p>
            </div>
          </div>
        </section>

        {/* Overview */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2">
            🔒 How It Works
          </h2>

          <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
            <p className="text-gray-300 mb-4">
              ascii-chat uses{" "}
              <strong className="text-purple-300">libsodium</strong> for
              cryptography—the same library that powers Signal, WireGuard, and
              Zcash.
            </p>
            <p className="text-gray-300">
              Every connection performs a Diffie-Hellman key exchange to
              establish a secure tunnel. Your video and audio are encrypted with{" "}
              <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
                XSalsa20-Poly1305
              </code>{" "}
              before leaving your machine.{" "}
              <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
                Ed25519
              </code>{" "}
              signatures verify peer identity,{" "}
              <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                X25519
              </code>{" "}
              <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                ECDH
              </code>{" "}
              provides perfect forward secrecy, and the{" "}
              <code className="text-cyan-400 bg-gray-950 px-2 py-1 rounded">
                XSalsa20-Poly1305
              </code>{" "}
              <code className="text-purple-400 bg-gray-950 px-2 py-1 rounded">
                AEAD
              </code>{" "}
              cipher protects all data in transit.
            </p>
          </div>
        </section>

        {/* Technical Details */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2">
            🔬 Technical Details
          </h2>

          <div className="space-y-6">
            <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
              <h3 className="text-cyan-300 font-semibold mb-3">
                Handshake Protocol
              </h3>
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
              <h3 className="text-purple-300 font-semibold mb-3">
                Encryption Algorithms
              </h3>
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
              <h3 className="text-teal-300 font-semibold mb-3">
                Perfect Forward Secrecy
              </h3>
              <p className="text-gray-300">
                Each connection generates new ephemeral keys. Even if your
                long-term SSH/GPG key is compromised, past sessions cannot be
                decrypted. Keys are never stored—only used for the duration of
                the connection.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
              <h3 className="text-pink-300 font-semibold mb-3">
                Session Rekeying
              </h3>
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
              <h3 className="text-cyan-300 font-semibold mb-3">
                Nonce Construction
              </h3>
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
                can't be replayed because the nonce is unique per packet and per
                session.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
              <h3 className="text-purple-300 font-semibold mb-3">
                Constant-Time Operations
              </h3>
              <p className="text-gray-300 mb-2">
                All cryptographic comparisons use constant-time algorithms (via{" "}
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
                of where the first difference occurs, preventing attackers from
                learning partial key/MAC values through timing side-channels.
              </p>
            </div>
          </div>
        </section>

        {/* SSH Keys */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2">
            🔑 SSH Key Authentication
          </h2>

          <p className="text-gray-300 mb-6">
            Use your existing SSH Ed25519 keys for authentication. ascii-chat
            reads the same keys you use for GitHub, servers, and git. (Configure
            these in your{" "}
            <TrackedLink
              to="/docs/configuration"
              label="ssh key config"
              className="text-cyan-400 hover:text-cyan-300 transition-colors"
            >
              config file
            </TrackedLink>
            )
          </p>

          <div className="space-y-6">
            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Server with SSH key
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">{`# Server authenticates with its key
ascii-chat server --key ~/.ssh/id_ed25519`}</code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Client connects with their key
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">{`# Client authenticates with their key
ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519`}</code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-pink-300 mb-3">
                Encrypted SSH keys
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Prompts for passphrase or uses ssh-agent
`}</span>
                  {`ascii-chat server --key ~/.ssh/id_ed25519_encrypted

`}
                  <span className="text-gray-500">{`# Or set passphrase via environment variable
`}</span>
                  {`export ASCII_CHAT_KEY_PASSWORD="my-passphrase"
ascii-chat server --key ~/.ssh/id_ed25519_encrypted`}
                </code>
              </pre>
              <p className="text-gray-400 text-sm mt-3">
                See the{" "}
                <TrackedLink
                  to="/man1#ENVIRONMENT"
                  label="Crypto - Env Vars Link"
                  className="text-green-400 hover:text-green-300 underline"
                >
                  Man Page ENVIRONMENT section
                </TrackedLink>{" "}
                for all available configuration options.
              </p>
            </div>
          </div>
        </section>

        {/* GPG Keys */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2">
            🛡️ GPG Key Authentication
          </h2>

          <p className="text-gray-300 mb-6">
            GPG Ed25519 keys work via{" "}
            <strong className="text-teal-300">gpg-agent</strong>. No passphrase
            prompts.
          </p>

          <div className="space-y-6">
            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Using GPG key ID
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server with GPG key (short/long/full fingerprint)
`}</span>
                  {`ascii-chat server --key gpg:7FE90A79F2E80ED3

`}
                  <span className="text-gray-500">{`# Client connects with their GPG key
`}</span>
                  {`ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3`}
                </code>
              </pre>
            </div>
          </div>
        </section>

        {/* Password Encryption */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2">
            🔐 Password-Based Encryption
          </h2>

          <p className="text-gray-300 mb-6">
            Simple password encryption for quick sessions. Combine with key
            authentication for defense-in-depth.
          </p>

          <div className="space-y-6">
            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Password-only
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server sets password
`}</span>
                  {`ascii-chat server --password "correct-horse-battery-staple"

`}
                  <span className="text-gray-500">{`# Client must know the password
`}</span>
                  {`ascii-chat client 192.168.1.100 --password "correct-horse-battery-staple"`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Key + Password (maximum security)
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Both SSH key and password required
`}</span>
                  {`ascii-chat server --key ~/.ssh/id_ed25519 --password "extra-secret"

`}
                  <span className="text-gray-500">{`# Client needs both to connect
`}</span>
                  {`ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519 --password "extra-secret"`}
                </code>
              </pre>
            </div>
          </div>
        </section>

        {/* Server Verification */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2">
            ✅ Server Identity Verification
          </h2>

          <p className="text-gray-300 mb-6">
            Prevent man-in-the-middle attacks by verifying the server's public
            key before connecting.
          </p>

          <div className="space-y-6">
            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Verify with local public key file
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Client verifies server identity
`}</span>
                  {`ascii-chat happy-sunset-ocean --server-key ~/.ssh/server1.pub`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-teal-300 mb-3">
                Verify with GitHub/GitLab GPG keys
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Fetches server's GPG keys from GitHub
`}</span>
                  {`ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg

`}
                  <span className="text-gray-500">{`# Or from GitLab
`}</span>
                  {`ascii-chat happy-sunset-ocean --server-key gitlab:zfogg.gpg`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-pink-300 mb-3">
                Verify with GPG key ID
              </h3>
              <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Verify against specific GPG key
`}</span>
                  {`ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}
                </code>
              </pre>
            </div>
          </div>
        </section>

        {/* Known Hosts */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2">
            📋 Known Hosts (TOFU)
          </h2>

          <p className="text-gray-300 mb-6">
            ascii-chat tracks server identities with an SSH-style{" "}
            <strong className="text-teal-300">Trust-On-First-Use (TOFU)</strong>{" "}
            model. The first time you connect to a server, its public key is
            saved. Subsequent connections verify the key hasn't changed.
          </p>

          <div className="space-y-6">
            <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
              <h3 className="text-cyan-300 font-semibold mb-3">
                File Location
              </h3>
              <p className="text-gray-300 mb-2">
                Known server keys are stored in:
              </p>
              <div className="space-y-2">
                <div>
                  <p className="text-gray-400 text-sm mb-1">
                    Unix/Linux/macOS:
                  </p>
                  <pre className="bg-gray-950 border border-gray-800 rounded p-3">
                    <code className="text-teal-300">
                      ~/.config/ascii-chat/known_hosts
                    </code>
                  </pre>
                  <p className="text-gray-400 text-xs mt-1">
                    (or{" "}
                    <code className="text-purple-400 bg-gray-950 px-1 py-0.5 rounded">
                      $XDG_CONFIG_HOME/ascii-chat/known_hosts
                    </code>{" "}
                    if XDG_CONFIG_HOME is set)
                  </p>
                </div>
                <div>
                  <p className="text-gray-400 text-sm mb-1">Windows:</p>
                  <pre className="bg-gray-950 border border-gray-800 rounded p-3">
                    <code className="text-teal-300">
                      %APPDATA%\ascii-chat\known_hosts
                    </code>
                  </pre>
                </div>
              </div>
              <p className="text-gray-300 text-sm mt-3">
                This file is created automatically on first connection. It's
                readable and editable as plain text.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
              <h3 className="text-purple-300 font-semibold mb-3">
                File Format
              </h3>
              <p className="text-gray-300 mb-3">
                Each line represents one known server:
              </p>
              <pre className="bg-gray-950 border border-gray-800 rounded p-3 overflow-x-auto">
                <code className="text-teal-300">{`<IP:port> x25519 <hex_public_key> [optional comment]`}</code>
              </pre>
              <p className="text-gray-300 mb-3 mt-3">Example entries:</p>
              <pre className="bg-gray-950 border border-gray-800 rounded p-3 overflow-x-auto text-sm">
                <code className="text-teal-300">{`192.168.1.100:27015 x25519 a1b2c3d4... server-laptop
10.0.0.5:27015 x25519 5e6f7890... production-server
[2001:db8::1]:27015 x25519 12345678... ipv6-server`}</code>
              </pre>
              <p className="text-gray-300 text-sm mt-3">
                <strong className="text-purple-400">Note:</strong> IPv6
                addresses use bracket notation{" "}
                <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                  [address]:port
                </code>{" "}
                to distinguish the colons in the address from the port
                separator.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
              <h3 className="text-teal-300 font-semibold mb-3">How It Works</h3>
              <ol className="list-decimal list-inside space-y-2 text-gray-300">
                <li>
                  <strong className="text-cyan-400">First Connection:</strong>{" "}
                  Server's key is saved to known_hosts automatically. Connection
                  proceeds.
                </li>
                <li>
                  <strong className="text-purple-400">
                    Subsequent Connections:
                  </strong>{" "}
                  Client checks if the server's key matches the saved key.
                </li>
                <li>
                  <strong className="text-teal-400">Key Matches:</strong>{" "}
                  Connection proceeds silently.
                </li>
                <li>
                  <strong className="text-pink-400">Key Mismatch:</strong>{" "}
                  Connection is rejected with a MITM warning. User must take
                  action.
                </li>
              </ol>
            </div>

            <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-6">
              <h3 className="text-yellow-300 font-semibold mb-3">
                ⚠️ When Keys Change (MITM Warning)
              </h3>
              <p className="text-gray-300 mb-3">
                If a server's key changes, you'll see a warning like:
              </p>
              <pre className="bg-gray-950 border border-red-800 rounded p-3 text-sm overflow-x-auto">
                <code className="text-red-300">{`@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!    @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!

The server's key has changed:
  Expected: a1b2c3d4...
  Received: 9f8e7d6c...

Connection refused. Remove the old key from known_hosts to continue.`}</code>
              </pre>
            </div>

            <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
              <h3 className="text-pink-300 font-semibold mb-3">
                🔧 What to Do When Keys Change
              </h3>
              <p className="text-gray-300 mb-4">
                Key changes happen for legitimate reasons (server reinstalled,
                key rotated) and malicious reasons (MITM attack). Verify before
                proceeding.
              </p>

              <div className="space-y-4">
                <div>
                  <h4 className="text-cyan-400 font-semibold mb-2">
                    Step 1: Verify the Key Change is Legitimate
                  </h4>
                  <ul className="list-disc list-inside space-y-1 text-gray-300 text-sm ml-4">
                    <li>
                      Contact the server administrator through a different
                      channel (phone, Signal, etc.)
                    </li>
                    <li>
                      Ask them to confirm they changed keys or reinstalled the
                      server
                    </li>
                    <li>
                      Verify the new key fingerprint matches what they provide
                    </li>
                  </ul>
                </div>

                <div>
                  <h4 className="text-purple-400 font-semibold mb-2">
                    Step 2: Remove the Old Entry
                  </h4>
                  <p className="text-gray-300 mb-2 text-sm">
                    Open{" "}
                    <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                      ~/.config/ascii-chat/known_hosts
                    </code>{" "}
                    in a text editor and delete the line for that IP:port
                    combination:
                  </p>
                  <pre className="bg-gray-950 border border-gray-800 rounded p-3 text-sm">
                    <code className="text-teal-300">{`# Before (remove this line):
192.168.1.100:27015 x25519 a1b2c3d4... old-key

# After (line deleted):`}</code>
                  </pre>
                  <p className="text-gray-300 text-sm mt-2">
                    Alternatively, delete the entire file to remove all known
                    hosts:{" "}
                    <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                      rm ~/.config/ascii-chat/known_hosts
                    </code>
                  </p>
                </div>

                <div>
                  <h4 className="text-teal-400 font-semibold mb-2">
                    Step 3: Reconnect
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Connect again. The new key will be saved automatically (TOFU
                    model).
                  </p>
                </div>
              </div>
            </div>

            <div className="bg-red-900/20 border border-red-700/50 rounded-lg p-6">
              <h3 className="text-red-300 font-semibold mb-3">
                🚨 Disabling known hosts (don't do this)
              </h3>
              <p className="text-gray-300 mb-3">
                Set{" "}
                <code className="text-red-400 bg-gray-950 px-2 py-1 rounded">
                  ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1
                </code>{" "}
                to skip known hosts checks.
              </p>
              <p className="text-red-300 font-semibold mb-2">
                ⚠️ This turns off MITM protection. Anyone can intercept your
                connection.
              </p>
              <p className="text-gray-300 text-sm">
                Only use this for local testing. See the{" "}
                <TrackedLink
                  to="/man1#ENVIRONMENT"
                  label="Crypto - Env Vars Insecure"
                  className="text-green-400 hover:text-green-300 underline"
                >
                  Man Page ENVIRONMENT section
                </TrackedLink>{" "}
                for details.
              </p>
            </div>
          </div>
        </section>

        {/* Key Whitelisting */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2">
            👥 Key Whitelisting
          </h2>

          <p className="text-gray-300 mb-6">
            Restrict connections to known peers by maintaining whitelists of
            trusted public keys. Works both ways: servers can whitelist clients,
            and clients can whitelist servers.
          </p>

          <div className="space-y-8">
            <div>
              <h3 className="text-2xl font-semibold text-cyan-400 mb-4">
                Server Whitelisting Clients
              </h3>
              <p className="text-gray-300 mb-4">
                Restrict which clients can connect to your server.
              </p>

              <div className="space-y-6">
                <div>
                  <h4 className="text-lg font-semibold text-cyan-300 mb-3">
                    Whitelist file (authorized_keys format)
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Create allowed_clients.txt with one public key per line
`}</span>
                      {`cat allowed_clients.txt
`}
                      <span className="text-gray-500">{`# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF... alice@example.com
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIG... bob@example.com

`}</span>
                      <span className="text-gray-500">{`# Server only accepts whitelisted clients
`}</span>
                      {`ascii-chat server --key ~/.ssh/id_ed25519 --client-keys allowed_clients.txt`}
                    </code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-lg font-semibold text-purple-300 mb-3">
                    Whitelist GitHub user's GPG keys
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Fetch all GPG keys from GitHub user
`}</span>
                      {`ascii-chat server --key gpg:MYKEYID --client-keys github:zfogg.gpg

`}
                      <span className="text-gray-500">{`# Client must authenticate with their GPG key
`}</span>
                      {`ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F6`}
                    </code>
                  </pre>
                </div>
              </div>
            </div>

            <div>
              <h3 className="text-2xl font-semibold text-teal-400 mb-4">
                Client Whitelisting Servers
              </h3>
              <p className="text-gray-300 mb-4">
                Only connect to servers with known, trusted keys.
              </p>

              <div className="space-y-6">
                <div>
                  <h4 className="text-lg font-semibold text-teal-300 mb-3">
                    Verify with local key file
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Client verifies server matches this exact key
`}</span>
                      {`ascii-chat happy-sunset-ocean --server-key ~/.ssh/known_server.pub`}
                    </code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-lg font-semibold text-pink-300 mb-3">
                    Verify with GitHub SSH keys
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Fetches server's SSH keys from GitHub
`}</span>
                      {`ascii-chat happy-sunset-ocean --server-key github:zfogg.keys`}
                    </code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-lg font-semibold text-cyan-300 mb-3">
                    Verify with GitHub GPG keys
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Fetches server's GPG keys from GitHub
`}</span>
                      {`ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg`}
                    </code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-lg font-semibold text-purple-300 mb-3">
                    Verify with GPG key ID
                  </h4>
                  <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
                    <code className="text-teal-300">
                      <span className="text-gray-500">{`# Verify against specific GPG key from keyring
`}</span>
                      {`ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}
                    </code>
                  </pre>
                </div>
              </div>
            </div>
          </div>
        </section>

        {/* No Encryption */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2">
            🚫 Disabling Encryption
          </h2>

          <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-6 mb-6">
            <p className="text-yellow-200">
              <strong>⚠️ Warning:</strong> Only disable encryption for local
              testing on trusted networks. Your video and audio will be sent
              unencrypted over the network.
            </p>
          </div>

          <pre className="bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto">
            <code className="text-teal-300">
              <span className="text-gray-500">{`# Server with encryption disabled
`}</span>
              {`ascii-chat server --no-encrypt

`}
              <span className="text-gray-500">{`# Client must also disable encryption
`}</span>
              {`ascii-chat client 127.0.0.1 --no-encrypt`}
            </code>
          </pre>

          <div className="bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4 mt-6">
            <p className="text-gray-300 text-sm">
              <strong className="text-cyan-300">Note:</strong> Both client and
              server must use{" "}
              <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                --no-encrypt
              </code>{" "}
              for unencrypted mode to work. If only one side disables
              encryption, the connection will fail during the handshake.
            </p>
          </div>
        </section>

        {/* Resources */}
        <section className="mb-16">
          <h2 className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2">
            📚 Learn More
          </h2>

          <div className="grid md:grid-cols-2 gap-4">
            <TrackedLink
              href="https://discovery.ascii-chat.com"
              label="Crypto - ACDS Server"
              target="_blank"
              rel="noopener noreferrer"
              className="bg-gray-900/50 border border-pink-900/50 rounded-lg p-4 hover:border-pink-500/50 transition-colors"
            >
              <h3 className="text-pink-300 font-semibold mb-1">
                🔍 Discovery Server
              </h3>
              <p className="text-gray-400 text-sm">
                Public keys and discovery service details
              </p>
            </TrackedLink>

            <TrackedLink
              href="https://zfogg.github.io/ascii-chat/group__handshake.html"
              label="Crypto - Handshake Protocol"
              target="_blank"
              rel="noopener noreferrer"
              className="bg-gray-900/50 border border-cyan-900/50 rounded-lg p-4 hover:border-cyan-500/50 transition-colors"
            >
              <h3 className="text-cyan-300 font-semibold mb-1">
                🔐 Handshake Protocol
              </h3>
              <p className="text-gray-400 text-sm">
                Detailed protocol documentation
              </p>
            </TrackedLink>

            <TrackedLink
              href="https://libsodium.gitbook.io/doc/"
              label="Crypto - libsodium Docs"
              target="_blank"
              rel="noopener noreferrer"
              className="bg-gray-900/50 border border-purple-900/50 rounded-lg p-4 hover:border-purple-500/50 transition-colors"
            >
              <h3 className="text-purple-300 font-semibold mb-1">
                📖 libsodium Docs
              </h3>
              <p className="text-gray-400 text-sm">
                Cryptography library reference
              </p>
            </TrackedLink>
          </div>
        </section>

        {/* Footer */}
        <Footer />
      </div>
    </div>
  );
}
