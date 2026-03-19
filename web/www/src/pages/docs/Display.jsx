import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import Footer from "../../components/Footer";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import ImportantNotesSection from "../../components/docs/display/ImportantNotesSection";
import RenderModesSection from "../../components/docs/display/RenderModesSection";
import ASCIIPalettesSection from "../../components/docs/display/ASCIIPalettesSection";
import ColorFiltersSection from "../../components/docs/display/ColorFiltersSection";
import AnimationsEffectsSection from "../../components/docs/display/AnimationsEffectsSection";
import FramerateSection from "../../components/docs/display/FramerateSection";
import VideoTransformsSection from "../../components/docs/display/VideoTransformsSection";
import QuickReferenceSection from "../../components/docs/display/QuickReferenceSection";

export default function Display() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Display", path: "/docs/display" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Display - ascii-chat Documentation"
        description="Render modes, ASCII palettes, color filters, animations, framerate, and video transforms for ascii-chat."
        url={`${SITES.MAIN}/docs/display`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-fuchsia-400">🎨</span> Display & Rendering
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Render modes, palettes, color filters, animations, framerate, and
              video transforms
            </p>
          </header>

          <ImportantNotesSection />
          <RenderModesSection />
          <ASCIIPalettesSection />
          <ColorFiltersSection />
          <AnimationsEffectsSection />
          <FramerateSection />
          <VideoTransformsSection />
          <QuickReferenceSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
