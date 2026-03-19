import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { useEffect } from "react";
import Footer from "../../components/Footer";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import QuickStartSection from "../../components/docs/snapshot/QuickStartSection";
import PipingRedirectionSection from "../../components/docs/snapshot/PipingRedirectionSection";
import SnapshotDelaySection from "../../components/docs/snapshot/SnapshotDelaySection";
import HTTPSnapshotsSection from "../../components/docs/snapshot/HTTPSnapshotsSection";
import DebuggingVerificationSection from "../../components/docs/snapshot/DebuggingVerificationSection";
import TipsGotchasSection from "../../components/docs/snapshot/TipsGotchasSection";
import ScriptingTestingSection from "../../components/docs/snapshot/ScriptingTestingSection";
import UseCasesSection from "../../components/docs/snapshot/UseCasesSection";

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
        title="Snapshot Mode - ascii-chat Documentation"
        description="Single-frame capture, scripting, and automation examples with ascii-chat snapshot mode."
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
