import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, TrackedLink, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";

export default function DocsHub() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
    ]);
  }, []);

  // Stable section colors across rerenders
  const colorOptions = [
    { colorClass: "accent-purple", textClass: "text-purple-300" },
    { colorClass: "accent-pink", textClass: "text-pink-300" },
    { colorClass: "accent-cyan", textClass: "text-cyan-300" },
    { colorClass: "accent-teal", textClass: "text-teal-300" },
    { colorClass: "accent-green", textClass: "text-green-300" },
    { colorClass: "accent-yellow", textClass: "text-yellow-300" },
    { colorClass: "accent-blue", textClass: "text-blue-300" },
    { colorClass: "accent-orange", textClass: "text-orange-300" },
    { colorClass: "accent-indigo", textClass: "text-indigo-300" },
    { colorClass: "accent-red", textClass: "text-red-300" },
  ];

  const docSections = [
    {
      icon: "⚙️",
      title: "Configuration",
      description: "Config files, options, color schemes, shell completions",
      to: "/docs/configuration",
      ...colorOptions[0],
    },
    {
      icon: "🎥",
      title: "Hardware",
      description: "Webcams, microphones, speakers, keyboard shortcuts",
      to: "/docs/hardware",
      ...colorOptions[1],
    },
    {
      icon: "🖥️",
      title: "Terminal",
      description: "Color modes, dimensions, UTF-8, terminal capabilities",
      to: "/docs/terminal",
      ...colorOptions[2],
    },
    {
      icon: "🎨",
      title: "Display",
      description:
        "Render modes, palettes, color filters, animations, framerate",
      to: "/docs/display",
      ...colorOptions[3],
    },
    {
      icon: "🎬",
      title: "Media Files and URLs",
      description: "Local files, remote URLs, YouTube, live streams, FFmpeg",
      to: "/docs/media",
      ...colorOptions[4],
    },
    {
      icon: "🌐",
      title: "Network",
      description: "Connections, discovery service, NAT traversal, protocols",
      to: "/docs/network",
      ...colorOptions[5],
    },
    {
      icon: "📸",
      title: "Snapshot Mode",
      description: "Single-frame capture, scripting, automation examples",
      to: "/docs/snapshot",
      ...colorOptions[6],
    },
    {
      icon: "🔐",
      title: "Crypto & Security",
      description: "Encryption, authentication, SSH keys, security flags, TOFU",
      to: "/docs/crypto",
      ...colorOptions[7],
    },
    {
      icon: "📖",
      title: "ascii-chat(1)",
      description: "Complete man page with all command-line options",
      to: "/man1",
      ...colorOptions[8],
    },
    {
      icon: "📋",
      title: "ascii-chat(5)",
      description: "File format and configuration syntax documentation",
      to: "/man5",
      ...colorOptions[9],
    },
    {
      icon: "📚",
      title: "ascii-chat-*(3)",
      description: "Source code documentation for ascii-chat and libasciichat",
      to: "/man3",
      ...colorOptions[0],
    },
    {
      icon: "🗺️",
      title: "Architecture & Flows",
      description:
        "Interactive diagrams: options pipeline, media, render-file, network protocol, crypto handshake, discovery",
      to: "/docs/architecture",
      ...colorOptions[1],
    },
  ];

  return (
    <>
      <AsciiChatHead
        title={pageMetadata.docs.title}
        description={pageMetadata.docs.description}
        url={`${SITES.MAIN}/docs`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
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
                    level={2}
                    className={`text-2xl ${section.textClass} font-semibold mb-2`}
                  >
                    {section.icon} {section.title}
                  </Heading>
                  <p className="text-gray-400 text-sm">{section.description}</p>
                </TrackedLink>
              ))}
            </div>
          </section>

          {/* Footer */}
          <Footer />
        </div>
      </div>
    </>
  );
}
