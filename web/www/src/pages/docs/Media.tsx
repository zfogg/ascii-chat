import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";
import {
  SupportedFormatsSection,
  LocalFilesSection,
  BroadcastingSection,
  RemoteURLsSection,
  PipingFFmpegSection,
  RenderingExportSection,
  AdvancedOptionsSection,
  UseCasesSection,
  PerformanceTipsSection,
} from "../../components/docs/media";

export default function Media() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Media Files and URLs", path: "/docs/media" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title={pageMetadata.media.title}
        description={pageMetadata.media.description}
        url={`${SITES.MAIN}/docs/media`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-yellow-400">🎬</span> Media Files and URLs
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Stream local files, remote URLs, live streams, and piped media as
              ASCII art
            </p>
          </header>

          <SupportedFormatsSection />
          <LocalFilesSection />
          <BroadcastingSection />
          <RemoteURLsSection />
          <PipingFFmpegSection />
          <RenderingExportSection />
          <AdvancedOptionsSection />
          <UseCasesSection />
          <PerformanceTipsSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
