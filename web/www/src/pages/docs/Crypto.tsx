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
  SecurityFlagsReferenceSection,
  SSHKeyAuthSection,
  GPGKeyAuthSection,
  KeyFlagArgumentsSection,
  PasswordEncryptionSection,
  ServerVerificationSection,
  KnownHostsSection,
  KeyWhitelistingSection,
  DiscoverySecuritySection,
  DisablingEncryptionSection,
  DisablingAuthSection,
  GotchasSection,
  LearnMoreSection,
} from "../../components/docs/crypto";

export default function Crypto() {
  useScrollToHash(100);
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Crypto & Security", path: "/docs/crypto" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Crypto & Security - ascii-chat"
        description="Encryption, keys, authentication, and security flags in ascii-chat. Ed25519, X25519, end-to-end encryption, and discovery service security."
        url={`${SITES.MAIN}/docs/crypto`}
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col flex-1">
        <div className="flex-1 flex flex-col docs-container">
          {/* Header */}
          <header className="mb-12 sm:mb-16">
            <Heading level={1} className="heading-1 mb-4">
              <span className="text-purple-400">🔐</span> Crypto & Security
            </Heading>
            <p className="text-lg sm:text-xl text-gray-300">
              End-to-end encryption with Ed25519 authentication, X25519 key
              exchange, and comprehensive security controls
            </p>
          </header>

          <AcdsNoteBox />
          <DesignPhilosophySection />
          <HowItWorksSection />
          <TechnicalDetailsSection />
          <SecurityFlagsReferenceSection />
          <SSHKeyAuthSection />
          <GPGKeyAuthSection />
          <KeyFlagArgumentsSection />
          <PasswordEncryptionSection />
          <ServerVerificationSection />
          <KnownHostsSection />
          <KeyWhitelistingSection />
          <DiscoverySecuritySection />
          <DisablingEncryptionSection />
          <DisablingAuthSection />
          <GotchasSection />
          <LearnMoreSection />

          <Footer />
        </div>
      </div>
    </>
  );
}
