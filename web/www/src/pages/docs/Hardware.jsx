import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import Footer from "../../components/Footer";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import ImportantNotesSection from "../../components/docs/hardware/ImportantNotesSection";
import WebcamSetupSection from "../../components/docs/hardware/WebcamSetupSection";
import AudioSetupSection from "../../components/docs/hardware/AudioSetupSection";
import MediaStreamingSection from "../../components/docs/hardware/MediaStreamingSection";
import DisplayOptionsSection from "../../components/docs/hardware/DisplayOptionsSection";
import KeyboardShortcutsSection from "../../components/docs/hardware/KeyboardShortcutsSection";

export default function Hardware() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Hardware", path: "/docs/hardware" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Hardware - ascii-chat Documentation"
        description="Learn about webcams, microphones, speakers, and keyboard shortcuts in ascii-chat."
        url={`${SITES.MAIN}/docs/hardware`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-pink-400">⚙️</span> Hardware Setup
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Webcam, microphone, speaker, and display configuration for
              ascii-chat
            </p>
          </header>

          <ImportantNotesSection />
          <WebcamSetupSection />
          <AudioSetupSection />
          <MediaStreamingSection />
          <DisplayOptionsSection />
          <KeyboardShortcutsSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
