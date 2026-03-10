/* global __SSH_PUBLIC_KEY__, __GPG_PUBLIC_KEY__ */
import { useEffect, useState } from "react";
import {
  Button,
  CodeBlock,
  Footer,
  GettingHelpSection,
  Heading,
  Link,
  PreCode,
  UsageExamplesSection,
} from "@ascii-chat/shared/components";
import {
  fetchSessionStrings,
  SITES,
  useScrollToHash,
} from "@ascii-chat/shared/utils";
import { ACDSHead } from "../components/ACDSHead";
import { getGpgFingerprint, getSshFingerprint } from "../utils/keyFingerprints";

function Home() {
  useScrollToHash(100);
  const [sshKey, setSshKey] = useState(__SSH_PUBLIC_KEY__ || "");
  const [gpgKey, setGpgKey] = useState(__GPG_PUBLIC_KEY__ || "");
  const [sshFingerprint, setSshFingerprint] = useState("");
  const [gpgFingerprint, setGpgFingerprint] = useState("");
  const [baseUrl] = useState(() => window.location.origin);
  const [sessionStrings, setSessionStrings] = useState([
    "hindu-batman-marriage",
    "jumbo-slayer-sergeant",
  ]);

  useEffect(() => {
    // Compute fingerprints from keys
    const computeFingerprints = async () => {
      if (sshKey) {
        const fp = await getSshFingerprint(sshKey);
        if (fp) setSshFingerprint(fp);
      }
      if (gpgKey) {
        const fp = await getGpgFingerprint(gpgKey);
        if (fp) setGpgFingerprint(fp);
      }
    };

    computeFingerprints();

    // Fetch session strings
    fetchSessionStrings(3)
      .then((strings) => setSessionStrings(strings))
      .catch((e) => console.error("Failed to load session strings:", e));
  }, [sshKey, gpgKey]);

  const handleSshDownload = () => {
    if (window.gtag) {
      window.gtag("event", "download_ssh_key");
    }
  };

  const handleGpgDownload = () => {
    if (window.gtag) {
      window.gtag("event", "download_gpg_key");
    }
  };

  const handleLinkClick = (url, text) => {
    if (window.gtag) {
      window.gtag("event", "link_click", {
        link_url: url,
        link_text: text,
      });
    }
  };

  return (
    <>
      <ACDSHead />
      <div className="max-w-4xl mx-auto px-4 md:px-8 py-8 md:py-16">
        <header className="text-center mb-12 pb-8 border-b-2 border-gray-700">
          <Heading
            level={1}
            className="mb-2 text-blue-400 text-3xl md:text-4xl"
          >
            🔍 ascii-chat Discovery Service
          </Heading>
          <p className="text-gray-400 text-lg md:text-xl m-0">
            Official Public Keys
          </p>
          <p className="text-gray-400 text-lg md:text-xl m-0">
            Session signalling for{" "}
            <Link
              href={SITES.MAIN}
              onClick={() =>
                handleLinkClick(SITES.MAIN, "ascii-chat website (header)")
              }
            >
              ascii-chat
            </Link>
          </p>
        </header>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            📋 About ACDS
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            The <strong>ascii-chat Discovery Service (ACDS)</strong> is a core
            component of{" "}
            <Link
              href={SITES.MAIN}
              onClick={() =>
                handleLinkClick(SITES.MAIN, "ascii-chat website (about)")
              }
            >
              ascii-chat
            </Link>
            , a real-time terminal-based video chat application. ACDS enables
            session discovery using memorable three-word strings like{" "}
            <code className="bg-gray-800 px-1 rounded">
              {sessionStrings[0]}
            </code>{" "}
            instead of IP addresses. It provides NAT traversal, WebRTC
            signaling, and peer-to-peer connection establishment.
          </p>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            <strong>Privacy-first:</strong> ACDS only exchanges connection
            metadata. Your audio and video never pass through our servers. All
            media flows peer-to-peer with end-to-end encryption by default.
          </p>

          <Heading
            level={3}
            className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
          >
            🏗️ Official ACDS Infrastructure
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            The official ACDS deployment consists of two components:
          </p>
          <ul className="leading-relaxed ml-0 pl-4 space-y-2">
            <li>
              <strong>This website</strong> (
              <code className="bg-gray-800 px-1 rounded">
                {window.location.hostname}
              </code>
              ) - Serves public keys over HTTPS
            </li>
            <li>
              <strong>Official ACDS server instance</strong> (
              <code className="bg-gray-800 px-1 rounded">
                tcp://discovery-service.ascii-chat.com:27225
              </code>
              ) - Handles session management (TCP)
            </li>
          </ul>
        </section>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            🔑 Public Keys
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            These Ed25519 public keys are used to verify the identity of the
            official ACDS server at{" "}
            <code className="bg-gray-800 px-1 rounded">
              tcp://discovery-service.ascii-chat.com:27225
            </code>
            . You may download and verify these keys before connecting.
          </p>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            The ascii-chat client is programmed to automatically download public
            keys over HTTPS from this site and connect to{" "}
            <code className="bg-gray-800 px-1 rounded">
              tcp://discovery-service.ascii-chat.com:27225
            </code>{" "}
            and trust it.
          </p>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            Keys are available at:
          </p>
          <ul className="leading-relaxed ml-0 pl-4 space-y-2">
            <li>
              <Link href="/key.pub">
                <code className="bg-gray-800 px-1 rounded">
                  {baseUrl}/key.pub
                </code>
              </Link>{" "}
              (SSH)
            </li>
            <li>
              <Link href="/key.gpg">
                <code className="bg-gray-800 px-1 rounded">
                  {baseUrl}/key.gpg
                </code>
              </Link>{" "}
              (GPG)
            </li>
          </ul>

          <Heading
            level={3}
            className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
          >
            SSH Ed25519 Public Key
          </Heading>
          <p className="leading-relaxed mb-2 text-base md:text-lg">
            <strong>Fingerprint:</strong>
          </p>
          <PreCode>
            {sshFingerprint ||
              (sshKey ? "..." : "(set SSH_PUBLIC_KEY env var at build time)")}
          </PreCode>
          <p className="leading-relaxed mb-2 text-base md:text-lg">
            <strong>Public Key:</strong>
          </p>
          <PreCode style={{ minHeight: "60px" }}>
            {sshKey || "(SSH_PUBLIC_KEY env var not set at build time)"}
          </PreCode>
          <Button
            variant="secondary"
            href="/key.pub"
            download
            className="inline-block mt-2 mb-4"
          >
            ⬇ Download SSH Public Key
          </Button>

          <Heading
            level={3}
            className="text-gray-200 mt-6 mb-2 text-xl md:text-2xl"
          >
            GPG Ed25519 Public Key
          </Heading>
          <p className="leading-relaxed mb-2 text-base md:text-lg">
            <strong>Fingerprint:</strong>
          </p>
          <PreCode>
            {gpgFingerprint ||
              (gpgKey ? "..." : "(set GPG_PUBLIC_KEY env var at build time)")}
          </PreCode>
          <p className="leading-relaxed mb-2 text-base md:text-lg">
            <strong>Public Key:</strong>
          </p>
          <PreCode style={{ minHeight: "200px" }}>
            {gpgKey || "(GPG_PUBLIC_KEY env var not set at build time)"}
          </PreCode>
          <Button
            variant="secondary"
            href="/key.gpg"
            download
            className="inline-block mt-2 mb-4"
          >
            ⬇ Download GPG Public Key
          </Button>
        </section>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            📖 Getting Help
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            For complete documentation and options, use the built-in help
            system:
          </p>
          <GettingHelpSection
            modeExample="discovery-service"
            introText=""
            headingClassName="sr-only"
          />
        </section>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            💻 Usage Examples
          </Heading>
          <UsageExamplesSection
            sessionString={sessionStrings[1]}
            headingClassName="sr-only"
          />
        </section>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            🏗️ Running Your Own ACDS Server
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            You can run a private ACDS server for your organization. Third-party
            ACDS servers require clients to explicitly configure your public key
            via the{" "}
            <code className="bg-gray-800 px-1 rounded">
              --discovery-service-key
            </code>{" "}
            flag.
          </p>
          <CodeBlock language="bash">
            {`# Start your own ACDS server with SSH and GPG keys
ascii-chat discovery-service 0.0.0.0 :: --port 27225 \\
  --key ~/.ssh/id_ed25519 \\
  --key gpg:YOUR_GPG_KEY_ID

# Server with GPG key
ascii-chat server --key gpg:SERVER_GPG_KEY_ID

# Client connects with explicit ACDS trust and authenticates with SSH key
ascii-chat session-name \\
  --discovery-service your-acds.example.com \\
  --discovery-service-key https://your-acds.example.com/key.pub \\
  --key ~/.ssh/id_ed25519 \\
  --server-key gpg:SERVER_GPG_KEY_ID`}
          </CodeBlock>
          <br />
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            <strong>Important:</strong> You should share the public key with
            ascii-chatters in a safe way. We recommend pre-sharing them safely
            somehow or hosting them on a website at a domain you control and
            serving them over HTTPS like we do.
          </p>
        </section>

        <section className="mb-12">
          <Heading
            level={2}
            className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
          >
            🔒 Security
          </Heading>
          <p className="leading-relaxed mb-4 text-base md:text-lg">
            The discovery service uses the same crypto protocol and code as the
            client/server. You can find more about the crypto protocol in the{" "}
            <Link
              href={SITES.CRYPTO_DOCS}
              onClick={() =>
                handleLinkClick(SITES.CRYPTO_DOCS, "Crypto docs (security)")
              }
            >
              ascii-chat docs
            </Link>
            . See the{" "}
            <Link
              href={SITES.MAIN + "/man1#SECURITY"}
              onClick={() =>
                handleLinkClick(
                  SITES.MAIN + "/man1#SECURITY",
                  "Man page SECURITY section",
                )
              }
            >
              man page
            </Link>{" "}
            for all discovery-service mode crypto flags.
          </p>
        </section>

        <Footer
          links={[
            {
              href: "https://github.com/zfogg/ascii-chat",
              label: "📦 GitHub",
              color: "text-cyan-400 hover:text-cyan-300",
              onClick: () =>
                handleLinkClick(
                  "https://github.com/zfogg/ascii-chat",
                  "GitHub (footer)",
                ),
            },
            {
              href: "https://zfogg.github.io/ascii-chat/group__module__acds.html",
              label: "📚 Documentation",
              color: "text-teal-400 hover:text-teal-300",
              onClick: () =>
                handleLinkClick(
                  "https://zfogg.github.io/ascii-chat/group__module__acds.html",
                  "Documentation (footer)",
                ),
            },
            {
              href: SITES.MAIN,
              label: "🌐 www",
              color: "text-pink-400 hover:text-pink-300",
              onClick: () => handleLinkClick(SITES.MAIN, "www"),
            },
            {
              href: SITES.WEB,
              label: "🌐 Web Client",
              color: "text-yellow-400 hover:text-yellow-300",
              onClick: () => handleLinkClick(SITES.WEB, "Web Client"),
            },
          ]}
          commitSha={__COMMIT_SHA__}
          onCommitClick={() =>
            handleLinkClick(
              `https://github.com/zfogg/ascii-chat/commit/${__COMMIT_SHA__}`,
              "Commit SHA",
            )
          }
          extraLine={
            <>
              ascii-chat Discovery Service · Hosted at{" "}
              <code className="bg-gray-800 px-1 rounded">
                {window.location.hostname}
              </code>
            </>
          }
        />
      </div>
    </>
  );
}

export default Home;
