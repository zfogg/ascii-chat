import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import { Footer, TrackedLink, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";
import {
  ImportantNotesSection,
  ColorControlSection,
  TerminalDimensionsSection,
  UnicodeTextSection,
  DebuggingDetectionSection,
  TipsTricksSection,
} from "../../components/docs/terminal";

export default function Terminal() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Terminal", path: "/docs/terminal" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title={pageMetadata.terminal.title}
        description={pageMetadata.terminal.description}
        url={`${SITES.MAIN}/docs/terminal`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-cyan-400">🖥️</span> Terminal Rendering
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Colors, dimensions, Unicode support, and terminal capabilities
            </p>
          </header>

          <ImportantNotesSection />
          <ColorControlSection />

          {/* Display & Rendering */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-green-400">
              🎨 Display & Rendering
            </Heading>

            <p className="docs-paragraph">
              For render modes, ASCII palettes, color filters, animations,
              framerate, and video transforms, see the{" "}
              <TrackedLink
                to="/docs/display"
                label="display docs"
                className="link-standard"
              >
                Display page
              </TrackedLink>
              .
            </p>
          </section>

          <TerminalDimensionsSection />
          <UnicodeTextSection />

          {/* Snapshot Mode */}
          <section className="docs-section-spacing">
            <Heading level={2} className="heading-2 text-orange-400">
              📸 Snapshot Mode
            </Heading>

            <p className="docs-paragraph">
              For comprehensive snapshot capture documentation, see the{" "}
              <a href="/docs/snapshot" className="link-standard">
                Snapshot page
              </a>
              .
            </p>
          </section>

          <DebuggingDetectionSection />
          <TipsTricksSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
