import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, TrackedLink, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";

export default function DocsHub() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
    ]);
  }, []);

  // Color palette for randomization
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

  const getRandomColor = () => {
    return colorOptions[Math.floor(Math.random() * colorOptions.length)];
  };

  const docSections = [
    {
      icon: "⚙️",
      title: "Configuration",
      description: "Config files, options, color schemes, shell completions",
      to: "/docs/configuration",
      ...getRandomColor(),
    },
    {
      icon: "🎥",
      title: "Hardware",
      description: "Webcams, microphones, speakers, keyboard shortcuts",
      to: "/docs/hardware",
      ...getRandomColor(),
    },
    {
      icon: "🖥️",
      title: "Terminal",
      description: "Color modes, dimensions, UTF-8, terminal capabilities",
      to: "/docs/terminal",
      ...getRandomColor(),
    },
    {
      icon: "🎨",
      title: "Display",
      description:
        "Render modes, palettes, color filters, animations, framerate",
      to: "/docs/display",
      ...getRandomColor(),
    },
    {
      icon: "🎬",
      title: "Media Files and URLs",
      description: "Local files, remote URLs, YouTube, live streams, FFmpeg",
      to: "/docs/media",
      ...getRandomColor(),
    },
    {
      icon: "🌐",
      title: "Network",
      description: "Connections, discovery service, NAT traversal, protocols",
      to: "/docs/network",
      ...getRandomColor(),
    },
    {
      icon: "📸",
      title: "Snapshot Mode",
      description: "Single-frame capture, scripting, automation examples",
      to: "/docs/snapshot",
      ...getRandomColor(),
    },
    {
      icon: "🔐",
      title: "Crypto & Security",
      description: "Encryption, authentication, SSH keys, security flags, TOFU",
      to: "/docs/crypto",
      ...getRandomColor(),
    },
    {
      icon: "📖",
      title: "ascii-chat(1)",
      description: "Complete man page with all command-line options",
      to: "/man1",
      ...getRandomColor(),
    },
    {
      icon: "📋",
      title: "ascii-chat(5)",
      description: "File format and configuration syntax documentation",
      to: "/man5",
      ...getRandomColor(),
    },
    {
      icon: "📚",
      title: "ascii-chat-*(3)",
      description: "Source code documentation for ascii-chat and libasciichat",
      to: "/man3",
      ...getRandomColor(),
    },
  ];

  return (
    <>
      <AsciiChatHead
        title="Documentation - ascii-chat"
        description="Complete guides for configuring, using, and scripting ascii-chat. Learn about configuration, hardware, terminal modes, snapshots, networking, and media."
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
