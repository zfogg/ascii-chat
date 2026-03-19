import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import Footer from "../../components/Footer";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import PriorityOverridesSection from "../../components/docs/configuration/PriorityOverridesSection";
import ConfigFilesSection from "../../components/docs/configuration/ConfigFilesSection";
import ConfigFileFormatSection from "../../components/docs/configuration/ConfigFileFormatSection";
import EnvironmentVariablesSection from "../../components/docs/configuration/EnvironmentVariablesSection";
import ManPagesSection from "../../components/docs/configuration/ManPagesSection";
import ShellCompletionsSection from "../../components/docs/configuration/ShellCompletionsSection";

export default function Configuration() {
  useScrollToHash(100);

  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Configuration", path: "/docs/configuration" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Configuration - ascii-chat Documentation"
        description="Learn about ascii-chat configuration: config files, command-line options, color schemes, and shell completions."
        url={`${SITES.MAIN}/docs/configuration`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-purple-400">⚙️</span> Configuration
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              Config files, locations, overrides, options, and shell completions
            </p>
          </header>

          <PriorityOverridesSection />
          <ConfigFilesSection />
          <ConfigFileFormatSection />
          <EnvironmentVariablesSection />
          <ManPagesSection />
          <ShellCompletionsSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
