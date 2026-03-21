import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";
import {
  QuickStartSection,
  PipingRedirectionSection,
  SnapshotDelaySection,
  HTTPSnapshotsSection,
  DebuggingVerificationSection,
  TipsGotchasSection,
  ScriptingTestingSection,
  UseCasesSection,
} from "../../components/docs/snapshot";

export default function Snapshot() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Snapshot Mode", path: "/docs/snapshot" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title={pageMetadata.snapshot.title}
        description={pageMetadata.snapshot.description}
        url={`${SITES.MAIN}/docs/snapshot`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-teal-400">📸</span> Snapshot Mode
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Single-frame capture, scripting, automation, and debugging
            </p>
          </header>

          <QuickStartSection />
          <PipingRedirectionSection />
          <SnapshotDelaySection />
          <HTTPSnapshotsSection />
          <DebuggingVerificationSection />
          <TipsGotchasSection />
          <ScriptingTestingSection />
          <UseCasesSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
