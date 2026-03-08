import { useEffect } from "react";
import { Heading, HeadingProvider } from "@ascii-chat/shared/components";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { SITES } from "@ascii-chat/shared/utils";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";

export default function DocsHub() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
    ]);
  }, []);

  const docSections = [
    {
      icon: "⚙️",
      title: "Configuration",
      description: "Config files, options, color schemes, shell completions",
      to: "/docs/configuration",
      colorClass: "accent-purple",
      textClass: "text-purple-300",
    },
    {
      icon: "🎥",
      title: "Hardware",
      description: "Webcams, microphones, speakers, keyboard shortcuts",
      to: "/docs/hardware",
      colorClass: "accent-pink",
      textClass: "text-pink-300",
    },
    {
      icon: "🖥️",
      title: "Terminal",
      description: "Color modes, render modes, dimensions, capabilities",
      to: "/docs/terminal",
      colorClass: "accent-cyan",
      textClass: "text-cyan-300",
    },
    {
      icon: "📸",
      title: "Snapshot Mode",
      description: "Single-frame capture, scripting, automation examples",
      to: "/docs/snapshot",
      colorClass: "accent-teal",
      textClass: "text-teal-300",
    },
    {
      icon: "🌐",
      title: "Network",
      description: "Connections, discovery service, NAT traversal, protocols",
      to: "/docs/network",
      colorClass: "accent-green",
      textClass: "text-green-300",
    },
    {
      icon: "🎬",
      title: "Media Files and URLs",
      description: "Local files, remote URLs, YouTube, live streams, FFmpeg",
      to: "/docs/media",
      colorClass: "accent-yellow",
      textClass: "text-yellow-300",
    },
    {
      icon: "📖",
      title: "ascii-chat(1)",
      description: "Complete man page with all command-line options",
      to: "/man1",
      colorClass: "accent-pink",
      textClass: "text-pink-300",
    },
    {
      icon: "📚",
      title: "ascii-chat-*(3)",
      description: "Source code documentation for ascii-chat and libasciichat",
      to: "/man3",
      colorClass: "accent-teal",
      textClass: "text-teal-300",
    },
    {
      icon: "🔐",
      title: "Cryptography",
      description: "Encryption, authentication, SSH keys, TOFU",
      to: "/crypto",
      colorClass: "accent-purple",
      textClass: "text-purple-300",
    },
  ];

  return (
    <>
      <AsciiChatHead
        title="Documentation - ascii-chat"
        description="Complete guides for configuring, using, and scripting ascii-chat. Learn about configuration, hardware, terminal modes, snapshots, networking, and media."
        url={`${SITES.MAIN}/docs`}
      />
      <HeadingProvider>
        <div className="bg-gray-950 text-gray-100 flex flex-col">
          <div className="flex-1 flex flex-col docs-container">
            {/* Header */}
            <header className="mb-12 sm:mb-16 text-center">
              <Heading level={1} className="heading-1 mb-4">
                📚 Documentation
              </Heading>
              <p className="text-lg sm:text-xl text-gray-300">
                Complete guides for configuring, using, and scripting ascii-chat
              </p>
            </header>

            {/* Documentation Grid */}
            <section className="docs-section-spacing">
              <div className="grid-cols-2-sm lg:grid-cols-3">
                {docSections.map((section) => (
                  <TrackedLink
                    key={section.to}
                    to={section.to}
                    label={`Docs - ${section.title}`}
                    className={`card ${section.colorClass} transition-colors`}
                  >
                    <Heading
                      level={3}
                      className={`text-2xl ${section.textClass} font-semibold mb-2`}
                    >
                      {section.icon} {section.title}
                    </Heading>
                    <p className="text-gray-400 text-sm">
                      {section.description}
                    </p>
                  </TrackedLink>
                ))}
              </div>
            </section>

            {/* Footer */}
            <Footer />
          </div>
        </div>
      </HeadingProvider>
    </>
  );
}
