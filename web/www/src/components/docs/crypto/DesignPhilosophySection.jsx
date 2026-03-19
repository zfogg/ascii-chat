import { Heading } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";
import { SITES } from "@ascii-chat/shared/utils";

export default function DesignPhilosophySection() {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-pink-400 mb-4 sm:mb-6 border-b border-pink-900/50 pb-2"
      >
        🎯 Design Philosophy
      </Heading>

      <div className="space-y-6">
        <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-4 sm:p-6">
          <Heading level={3} className="text-pink-300 font-semibold mb-3">
            The Core Problem: Trust Without Infrastructure
          </Heading>
          <p className="text-gray-300 mb-3">
            HTTPS can rely on Certificate Authorities because there's a
            globally trusted PKI. ascii-chat has no such infrastructure
            for raw TCP connections. Yet users need both{" "}
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
          <Heading
            level={3}
            className="text-purple-300 font-semibold mb-3"
          >
            Five Levels of Security
          </Heading>
          <div className="space-y-3 text-gray-300">
            <div className="bg-gray-950/50 border border-cyan-900/30 rounded p-3">
              <strong className="text-red-400">
                Level 1: Default Encrypted but Unauthenticated
                (Vulnerable)
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
              <em>bound to the DH shared secret</em>. Prevents MITM
              attacks even without pre-shared keys.
            </div>
            <div className="bg-gray-950/50 border border-teal-900/30 rounded p-3">
              <strong className="text-teal-400">
                Level 3: SSH or GPG Key Pinning
              </strong>
              <br />
              Leverage existing SSH or GPG keys. Server signs ephemeral
              key; client verifies signature. Known hosts tracking (TOFU
              model).
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
          <Heading level={3} className="text-cyan-300 font-semibold mb-3">
            The ACDS Trust Model: Bootstrapping Trust Over HTTPS
          </Heading>
          <p className="text-gray-300 mb-3">
            ACDS (ASCII-Chat Discovery Service) solves the trust problem
            by using the{" "}
            <strong className="text-cyan-400">existing HTTPS PKI</strong>{" "}
            to bootstrap trust for raw TCP connections:
          </p>
          <ol className="list-decimal list-inside space-y-2 text-gray-300">
            <li>
              Client downloads ACDS public key from{" "}
              <TrackedLink
                href={`${SITES.DISCOVERY}/key.pub`}
                label="Crypto - ACDS SSH Key"
                target="_blank"
                rel="noopener noreferrer"
                className="text-pink-400 hover:text-pink-300 transition-colors underline"
              >
                {`${SITES.DISCOVERY}/key.pub`}
              </TrackedLink>{" "}
              (SSH) or{" "}
              <TrackedLink
                href={`${SITES.DISCOVERY}/key.gpg`}
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
            Chain of trust:{" "}
            <span className="text-cyan-400">HTTPS CA</span> →{" "}
            <span className="text-purple-400">ACDS key</span> →{" "}
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
              <strong className="text-cyan-300">Note:</strong> For
              complete ACDS public key information and fingerprints, visit{" "}
              <TrackedLink
                href={SITES.DISCOVERY}
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
          <Heading level={3} className="text-teal-300 font-semibold mb-3">
            Why These Design Choices?
          </Heading>
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
              secrecy." Even if long-term key is compromised, past
              sessions remain secret.
            </li>
            <li>
              <strong className="text-pink-400">Session rekeying:</strong>{" "}
              Limits impact of key compromise. Automatic rotation after 1
              hour or 1 million packets—whichever comes first.
            </li>
            <li>
              <strong className="text-cyan-400">
                Known hosts (TOFU):
              </strong>{" "}
              SSH-style trust model. First connection establishes trust;
              subsequent connections verify it hasn't changed. Key changes
              trigger MITM warnings.
            </li>
          </ul>
        </div>

        <div className="bg-purple-900/20 border border-purple-700/50 rounded-lg p-6">
          <Heading
            level={3}
            className="text-purple-300 font-semibold mb-3"
          >
            💡 Key Insight
          </Heading>
          <p className="text-gray-300">
            You can mix and match verification methods. Password-only for
            quick sessions. SSH keys for stronger identity. ACDS for
            zero-config. Stack all three if you want. More verification =
            stronger security, but defaults work without configuration.
          </p>
        </div>
      </div>
    </section>
  );
}
