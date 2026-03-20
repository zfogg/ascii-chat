import { useEffect } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import { Footer, AsciiChatHead } from "../../components";
import { setBreadcrumbSchema, useScrollToHash } from "../../utils";
import {
  AcdsNoteBox,
  DesignPhilosophySection,
  HowItWorksSection,
  TechnicalDetailsSection,
  SSHKeyAuthSection,
  GPGKeyAuthSection,
  PasswordEncryptionSection,
  ServerVerificationSection,
  KnownHostsSection,
  KeyWhitelistingSection,
  DisablingEncryptionSection,
  LearnMoreSection,
} from "../../components/docs/crypto";

export default function Crypto() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Cryptography", path: "/crypto" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Cryptography - ascii-chat"
        description="Encryption, keys, and authentication in ascii-chat. Learn about Ed25519, X25519, and end-to-end encryption."
        url={`${SITES.MAIN}/crypto`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col max-w-4xl mx-auto px-4 sm:px-6 py-8 sm:py-12 w-full">
          {/* Header */}
          <header className="mb-12 sm:mb-16">
            <Heading
              level={1}
              className="text-3xl sm:text-4xl md:text-5xl font-bold mb-4"
            >
              <span className="text-purple-400">🔐</span> Cryptography
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              End-to-end encryption with Ed25519 authentication and X25519 key
              exchange
            </p>
          </header>

          <AcdsNoteBox />
          <DesignPhilosophySection />
          <HowItWorksSection />
          <TechnicalDetailsSection />
          <SSHKeyAuthSection />
          <GPGKeyAuthSection />
          <PasswordEncryptionSection />
          <ServerVerificationSection />
          <KnownHostsSection />
          <KeyWhitelistingSection />
          <DisablingEncryptionSection />
          <LearnMoreSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
