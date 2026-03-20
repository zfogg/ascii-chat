/* global __SSH_PUBLIC_KEY__, __GPG_PUBLIC_KEY__ */
import { useEffect, useState } from "react";
import { Footer } from "@ascii-chat/shared/components";
import {
  fetchSessionStrings,
  SITES,
  useScrollToHash,
} from "@ascii-chat/shared/utils";
import { ACDSHead } from "../components";
import { getGpgFingerprint, getSshFingerprint } from "../utils/keyFingerprints";
import {
  HeroHeader,
  AboutSection,
  PublicKeysSection,
  GettingHelpWrapper,
  UsageExamplesWrapper,
  SelfHostingSection,
  SecuritySection,
} from "../components/home";

function Home() {
  useScrollToHash(100);
  const [sshKey, _setSshKey] = useState(__SSH_PUBLIC_KEY__ || "");
  const [gpgKey, _setGpgKey] = useState(__GPG_PUBLIC_KEY__ || "");
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

    void computeFingerprints();

    // Fetch session strings
    fetchSessionStrings(3)
      .then((strings) => setSessionStrings(strings))
      .catch((e) => console.error("Failed to load session strings:", e));
  }, [sshKey, gpgKey]);

  const _handleSshDownload = () => {
    if (window.gtag) {
      window.gtag("event", "download_ssh_key");
    }
  };

  const _handleGpgDownload = () => {
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
        <HeroHeader handleLinkClick={handleLinkClick} />

        <AboutSection
          sessionStrings={sessionStrings}
          handleLinkClick={handleLinkClick}
        />

        <PublicKeysSection
          sshKey={sshKey}
          gpgKey={gpgKey}
          sshFingerprint={sshFingerprint}
          gpgFingerprint={gpgFingerprint}
          baseUrl={baseUrl}
        />

        <GettingHelpWrapper />

        <UsageExamplesWrapper sessionStrings={sessionStrings} />

        <SelfHostingSection />

        <SecuritySection handleLinkClick={handleLinkClick} />

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
