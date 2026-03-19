import { useEffect } from "react";
import Footer from "../../components/Footer";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { useScrollToHash } from "../../utils/hooks";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import ACIPProtocolSection from "../../components/docs/network/ACIPProtocolSection";
import ConnectionFlowSection from "../../components/docs/network/ConnectionFlowSection";
import DiscoveryModeSection from "../../components/docs/network/DiscoveryModeSection";
import TraditionalModeSection from "../../components/docs/network/TraditionalModeSection";
import ACDSDiscoverySection from "../../components/docs/network/ACDSDiscoverySection";
import NATTraversalSection from "../../components/docs/network/NATTraversalSection";
import TroubleshootingSection from "../../components/docs/network/TroubleshootingSection";

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
        title="Network - ascii-chat Documentation"
        description="Connections, discovery service, NAT traversal, and network protocols for ascii-chat."
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
