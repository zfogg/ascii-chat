import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";
import {
  PriorityOverridesSection,
  ConfigFilesSection,
  ConfigFileFormatSection,
  EnvironmentVariablesSection,
  ManPagesSection,
  ShellCompletionsSection,
} from "../../components/docs/configuration";

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
        title={pageMetadata.configuration.title}
        description={pageMetadata.configuration.description}
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
