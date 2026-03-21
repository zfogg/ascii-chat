import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import { pageMetadata } from "../../metadata";
import {
  ACIPProtocolSection,
  ConnectionFlowSection,
  DiscoveryModeSection,
  TraditionalModeSection,
  ACDSDiscoverySection,
  NATTraversalSection,
  TroubleshootingSection,
} from "../../components/docs/network";

export default function Network() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Network", path: "/docs/network" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title={pageMetadata.network.title}
        description={pageMetadata.network.description}
        url={`${SITES.MAIN}/docs/network`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-green-400">🌐</span> Network & ACIP Protocol
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              ACIP protocol architecture, ACDS discovery, NAT traversal, and P2P
              connections
            </p>
          </header>

          <ACIPProtocolSection />
          <ConnectionFlowSection />
          <DiscoveryModeSection />
          <TraditionalModeSection />
          <ACDSDiscoverySection />
          <NATTraversalSection />
          <TroubleshootingSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
