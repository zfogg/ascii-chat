import { useEffect } from "react";
import Footer from "../../components/Footer";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import AcdsNoteBox from "../../components/docs/crypto/AcdsNoteBox";
import DesignPhilosophySection from "../../components/docs/crypto/DesignPhilosophySection";
import HowItWorksSection from "../../components/docs/crypto/HowItWorksSection";
import TechnicalDetailsSection from "../../components/docs/crypto/TechnicalDetailsSection";
import SSHKeyAuthSection from "../../components/docs/crypto/SSHKeyAuthSection";
import GPGKeyAuthSection from "../../components/docs/crypto/GPGKeyAuthSection";
import PasswordEncryptionSection from "../../components/docs/crypto/PasswordEncryptionSection";
import ServerVerificationSection from "../../components/docs/crypto/ServerVerificationSection";
import KnownHostsSection from "../../components/docs/crypto/KnownHostsSection";
import KeyWhitelistingSection from "../../components/docs/crypto/KeyWhitelistingSection";
import DisablingEncryptionSection from "../../components/docs/crypto/DisablingEncryptionSection";
import LearnMoreSection from "../../components/docs/crypto/LearnMoreSection";

export default function Crypto() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Cryptography", path: "/crypto" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Cryptography - ascii-chat"
        description="Encryption, keys, and authentication in ascii-chat. Learn about Ed25519, X25519, and end-to-end encryption."
        url={`${SITES.MAIN}/crypto`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col max-w-4xl mx-auto px-4 sm:px-6 py-8 sm:py-12 w-full">
          {/* Header */}
          <header className="mb-12 sm:mb-16">
            <Heading
              level={1}
              className="text-3xl sm:text-4xl md:text-5xl font-bold mb-4"
            >
              <span className="text-purple-400">🔐</span> Cryptography
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              End-to-end encryption with Ed25519 authentication and X25519 key
              exchange
            </p>
          </header>

          <AcdsNoteBox />
          <DesignPhilosophySection />
          <HowItWorksSection />
          <TechnicalDetailsSection />
          <SSHKeyAuthSection />
          <GPGKeyAuthSection />
          <PasswordEncryptionSection />
          <ServerVerificationSection />
          <KnownHostsSection />
          <KeyWhitelistingSection />
          <DisablingEncryptionSection />
          <LearnMoreSection />

          <Footer />
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

          {/* SSH Keys */}
          <section className="mb-16">
            <Heading
              level={2}
              className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2"
            >
              🔑 SSH Key Authentication
            </Heading>

            <p className="text-gray-300 mb-6">
              Use your existing SSH Ed25519 keys for authentication. ascii-chat
              reads the same keys you use for GitHub, servers, and git.
              (Configure these in your{" "}
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
                <Heading
                  level={3}
                  className="text-xl font-semibold text-cyan-300 mb-3"
                >
                  Server with SSH key
                </Heading>
                <CodeBlock language="bash">{`# Server authenticates with its key
ascii-chat server --key ~/.ssh/id_ed25519`}</CodeBlock>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-purple-300 mb-3"
                >
                  Client connects with their key
                </Heading>
                <CodeBlock language="bash">{`# Client authenticates with their key
ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519`}</CodeBlock>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-pink-300 mb-3"
                >
                  Encrypted SSH keys
                </Heading>
                <CodeBlock language="bash">{`# Prompts for passphrase or uses ssh-agent
ascii-chat server --key ~/.ssh/id_ed25519_encrypted

# Or set passphrase via environment variable
export ASCII_CHAT_KEY_PASSWORD="my-passphrase"
ascii-chat server --key ~/.ssh/id_ed25519_encrypted`}</CodeBlock>
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
            <Heading
              level={2}
              className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
            >
              🛡️ GPG Key Authentication
            </Heading>

            <p className="text-gray-300 mb-6">
              GPG Ed25519 keys work via{" "}
              <strong className="text-teal-300">gpg-agent</strong>. No
              passphrase prompts.
            </p>

            <div className="space-y-6">
              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-cyan-300 mb-3"
                >
                  Using GPG key ID
                </Heading>
                <CodeBlock language="bash">{`# Server with GPG key (short/long/full fingerprint)
ascii-chat server --key gpg:7FE90A79F2E80ED3

# Client connects with their GPG key
ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F612710AF97FE90A79F2E80ED3`}</CodeBlock>
              </div>
            </div>
          </section>

          {/* Password Encryption */}
          <section className="mb-16">
            <Heading
              level={2}
              className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2"
            >
              🔐 Password-Based Encryption
            </Heading>

            <p className="text-gray-300 mb-6">
              Simple password encryption for quick sessions. Combine with key
              authentication for defense-in-depth.
            </p>

            <div className="space-y-6">
              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-purple-300 mb-3"
                >
                  Password-only
                </Heading>
                <CodeBlock language="bash">{`# Server sets password
ascii-chat server --password "correct-horse-battery-staple"

# Client must know the password
ascii-chat client 192.168.1.100 --password "correct-horse-battery-staple"`}</CodeBlock>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-cyan-300 mb-3"
                >
                  Key + Password (maximum security)
                </Heading>
                <CodeBlock language="bash">{`# Both SSH key and password required
ascii-chat server --key ~/.ssh/id_ed25519 --password "extra-secret"

# Client needs both to connect
ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519 --password "extra-secret"`}</CodeBlock>
              </div>
            </div>
          </section>

          {/* Server Verification */}
          <section className="mb-16">
            <Heading
              level={2}
              className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
            >
              ✅ Server Identity Verification
            </Heading>

            <p className="text-gray-300 mb-6">
              Prevent man-in-the-middle attacks by verifying the server's public
              key before connecting.
            </p>

            <div className="space-y-6">
              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-purple-300 mb-3"
                >
                  Verify with local public key file
                </Heading>
                <CodeBlock language="bash">{`# Client verifies server identity
ascii-chat happy-sunset-ocean --server-key ~/.ssh/server1.pub`}</CodeBlock>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-teal-300 mb-3"
                >
                  Verify with GitHub/GitLab GPG keys
                </Heading>
                <CodeBlock language="bash">{`# Fetches server's GPG keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg

# Or from GitLab
ascii-chat happy-sunset-ocean --server-key gitlab:zfogg.gpg`}</CodeBlock>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-xl font-semibold text-pink-300 mb-3"
                >
                  Verify with GPG key ID
                </Heading>
                <CodeBlock language="bash">{`# Verify against specific GPG key
ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}</CodeBlock>
              </div>
            </div>
          </section>

          {/* Known Hosts */}
          <section className="mb-16">
            <Heading
              level={2}
              className="text-3xl font-bold text-teal-400 mb-6 border-b border-teal-900/50 pb-2"
            >
              📋 Known Hosts (TOFU)
            </Heading>

            <p className="text-gray-300 mb-6">
              ascii-chat tracks server identities with an SSH-style{" "}
              <strong className="text-teal-300">
                Trust-On-First-Use (TOFU)
              </strong>{" "}
              model. The first time you connect to a server, its public key is
              saved. Subsequent connections verify the key hasn't changed.
            </p>

            <div className="space-y-6">
              <div className="bg-gray-900/50 border border-cyan-900/30 rounded-lg p-6">
                <Heading level={3} className="text-cyan-300 font-semibold mb-3">
                  File Location
                </Heading>
                <p className="text-gray-300 mb-2">
                  Known server keys are stored in:
                </p>
                <div className="space-y-2">
                  <div>
                    <p className="text-gray-400 text-sm mb-1">
                      Unix/Linux/macOS:
                    </p>
                    <CodeBlock language="text">{``}</CodeBlock>
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
                    <CodeBlock language="text">{``}</CodeBlock>
                  </div>
                </div>
                <p className="text-gray-300 text-sm mt-3">
                  This file is created automatically on first connection. It's
                  readable and editable as plain text.
                </p>
              </div>

              <div className="bg-gray-900/50 border border-purple-900/30 rounded-lg p-6">
                <Heading
                  level={3}
                  className="text-purple-300 font-semibold mb-3"
                >
                  File Format
                </Heading>
                <p className="text-gray-300 mb-3">
                  Each line represents one known server:
                </p>
                <CodeBlock language="text">{`<IP:port> x25519 <hex_public_key> [optional comment]`}</CodeBlock>
                <p className="text-gray-300 mb-3 mt-3">Example entries:</p>
                <CodeBlock language="text">{`192.168.1.100:27015 x25519 a1b2c3d4... server-laptop
10.0.0.5:27015 x25519 5e6f7890... production-server
[2001:db8::1]:27015 x25519 12345678... ipv6-server`}</CodeBlock>
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
                <Heading level={3} className="text-teal-300 font-semibold mb-3">
                  How It Works
                </Heading>
                <ol className="list-decimal list-inside space-y-2 text-gray-300">
                  <li>
                    <strong className="text-cyan-400">First Connection:</strong>{" "}
                    Server's key is saved to known_hosts automatically.
                    Connection proceeds.
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
                <Heading
                  level={3}
                  className="text-yellow-300 font-semibold mb-3"
                >
                  ⚠️ When Keys Change (MITM Warning)
                </Heading>
                <p className="text-gray-300 mb-3">
                  If a server's key changes, you'll see a warning like:
                </p>
                <CodeBlock language="text">{`@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!    @
@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!
Someone could be eavesdropping on you right now (man-in-the-middle attack)!

The server's key has changed:
  Expected: a1b2c3d4...
  Received: 9f8e7d6c...

Connection refused. Remove the old key from known_hosts to continue.`}</CodeBlock>
              </div>

              <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-6">
                <Heading level={3} className="text-pink-300 font-semibold mb-3">
                  🔧 What to Do When Keys Change
                </Heading>
                <p className="text-gray-300 mb-4">
                  Key changes happen for legitimate reasons (server reinstalled,
                  key rotated) and malicious reasons (MITM attack). Verify
                  before proceeding.
                </p>

                <div className="space-y-4">
                  <div>
                    <Heading
                      level={4}
                      className="text-cyan-400 font-semibold mb-2"
                    >
                      Step 1: Verify the Key Change is Legitimate
                    </Heading>
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
                    <Heading
                      level={4}
                      className="text-purple-400 font-semibold mb-2"
                    >
                      Step 2: Remove the Old Entry
                    </Heading>
                    <p className="text-gray-300 mb-2 text-sm">
                      Open{" "}
                      <code className="text-teal-400 bg-gray-950 px-2 py-1 rounded">
                        ~/.config/ascii-chat/known_hosts
                      </code>{" "}
                      in a text editor and delete the line for that IP:port
                      combination:
                    </p>
                    <CodeBlock language="text">{`# Before (remove this line):
192.168.1.100:27015 x25519 a1b2c3d4... old-key

# After (line deleted):`}</CodeBlock>
                    <p className="text-gray-300 text-sm mt-2">
                      Alternatively, delete the entire file to remove all known
                      hosts:{" "}
                      <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                        rm ~/.config/ascii-chat/known_hosts
                      </code>
                    </p>
                  </div>

                  <div>
                    <Heading
                      level={4}
                      className="text-teal-400 font-semibold mb-2"
                    >
                      Step 3: Reconnect
                    </Heading>
                    <p className="text-gray-300 text-sm">
                      Connect again. The new key will be saved automatically
                      (TOFU model).
                    </p>
                  </div>
                </div>
              </div>

              <div className="bg-red-900/20 border border-red-700/50 rounded-lg p-6">
                <Heading level={3} className="text-red-300 font-semibold mb-3">
                  🚨 Disabling known hosts (don't do this)
                </Heading>
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
            <Heading
              level={2}
              className="text-3xl font-bold text-purple-400 mb-6 border-b border-purple-900/50 pb-2"
            >
              👥 Key Whitelisting
            </Heading>

            <p className="text-gray-300 mb-6">
              Restrict connections to known peers by maintaining whitelists of
              trusted public keys. Works both ways: servers can whitelist
              clients, and clients can whitelist servers.
            </p>

            <div className="space-y-8">
              <div>
                <Heading
                  level={3}
                  className="text-2xl font-semibold text-cyan-400 mb-4"
                >
                  Server Whitelisting Clients
                </Heading>
                <p className="text-gray-300 mb-4">
                  Restrict which clients can connect to your server.
                </p>

                <div className="space-y-6">
                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-cyan-300 mb-3"
                    >
                      Whitelist file (authorized_keys format)
                    </Heading>
                    <CodeBlock language="bash">{`# Create allowed_clients.txt with one public key per line
cat allowed_clients.txt
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIF... alice@example.com
# ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIG... bob@example.com

# Server only accepts whitelisted clients
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys allowed_clients.txt`}</CodeBlock>
                  </div>

                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-purple-300 mb-3"
                    >
                      Whitelist GitHub user's GPG keys
                    </Heading>
                    <CodeBlock language="bash">{`# Fetch all GPG keys from GitHub user
ascii-chat server --key gpg:MYKEYID --client-keys github:zfogg.gpg

# Client must authenticate with their GPG key
ascii-chat happy-sunset-ocean --key gpg:897607FA43DC66F6`}</CodeBlock>
                  </div>
                </div>
              </div>

              <div>
                <Heading
                  level={3}
                  className="text-2xl font-semibold text-teal-400 mb-4"
                >
                  Client Whitelisting Servers
                </Heading>
                <p className="text-gray-300 mb-4">
                  Only connect to servers with known, trusted keys.
                </p>

                <div className="space-y-6">
                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-teal-300 mb-3"
                    >
                      Verify with local key file
                    </Heading>
                    <CodeBlock language="bash">{`# Client verifies server matches this exact key
ascii-chat happy-sunset-ocean --server-key ~/.ssh/known_server.pub`}</CodeBlock>
                  </div>

                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-pink-300 mb-3"
                    >
                      Verify with GitHub SSH keys
                    </Heading>
                    <CodeBlock language="bash">{`# Fetches server's SSH keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:zfogg.keys`}</CodeBlock>
                  </div>

                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-cyan-300 mb-3"
                    >
                      Verify with GitHub GPG keys
                    </Heading>
                    <CodeBlock language="bash">{`# Fetches server's GPG keys from GitHub
ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg`}</CodeBlock>
                  </div>

                  <div>
                    <Heading
                      level={4}
                      className="text-lg font-semibold text-purple-300 mb-3"
                    >
                      Verify with GPG key ID
                    </Heading>
                    <CodeBlock language="bash">{`# Verify against specific GPG key from keyring
ascii-chat happy-sunset-ocean --server-key gpg:897607FA43DC66F6`}</CodeBlock>
                  </div>
                </div>
              </div>
            </div>
          </section>

          {/* No Encryption */}
          <section className="mb-16">
            <Heading
              level={2}
              className="text-3xl font-bold text-pink-400 mb-6 border-b border-pink-900/50 pb-2"
            >
              🚫 Disabling Encryption
            </Heading>

            <div className="bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-6 mb-6">
              <p className="text-yellow-200">
                <strong>⚠️ Warning:</strong> Only disable encryption for local
                testing on trusted networks. Your video and audio will be sent
                unencrypted over the network.
              </p>
            </div>

            <CodeBlock language="bash">{`# Server with encryption disabled
ascii-chat server --no-encrypt

# Client must also disable encryption
ascii-chat client 127.0.0.1 --no-encrypt`}</CodeBlock>

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
            <Heading
              level={2}
              className="text-3xl font-bold text-cyan-400 mb-6 border-b border-cyan-900/50 pb-2"
            >
              📚 Learn More
            </Heading>

            <div className="grid md:grid-cols-2 gap-4">
              <TrackedLink
                href={SITES.DISCOVERY}
                label="Crypto - ACDS Server"
                target="_blank"
                rel="noopener noreferrer"
                className="bg-gray-900/50 border border-pink-900/50 rounded-lg p-4 hover:border-pink-500/50 transition-colors"
              >
                <Heading level={3} className="text-pink-300 font-semibold mb-1">
                  🔍 Discovery Server
                </Heading>
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
                <Heading level={3} className="text-cyan-300 font-semibold mb-1">
                  🔐 Handshake Protocol
                </Heading>
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
                <Heading
                  level={3}
                  className="text-purple-300 font-semibold mb-1"
                >
                  📖 libsodium Docs
                </Heading>
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
    </>
  );
}
