import { useEffect, useState } from "react";
import { fetchSessionStrings } from "@ascii-chat/shared/utils";
import { useAnchorNavigation } from "../hooks";
import { AsciiChatHead, Footer } from "../components";
import {
  HeroHeader,
  InstallationSection,
  FeaturesSection,
  QuickStartSection,
  DocumentationSection,
  UsageExamplesSection,
  OpenSourceSection,
} from "../components/home";

export default function Home() {
  const [sessionStrings, setSessionStrings] = useState([
    "agricultural-thursday-accidental",
    "lively-masterpiece-partnership",
    "tipsy-apron-presence",
    "everyday-slide-guild",
    "american-berry-countryside",
    "paid-martial-forty",
    "online-fame-standby",
    "irritable-cappuccino-smoke",
  ]);
  const [contentLoaded, setContentLoaded] = useState(true);

  useAnchorNavigation(contentLoaded);

  useEffect(() => {
    fetchSessionStrings(7)
      .then((strings) => {
        if (strings.length > 0) {
          setSessionStrings(strings);
        }
        setContentLoaded(true);
      })
      .catch((e) => {
        console.error("Failed to load session strings:", e);
        setContentLoaded(true);
      });
  }, []);

  return (
    <>
      <AsciiChatHead />
      <div className="flex flex-col max-w-4xl mx-auto px-4 sm:px-6 w-full mt-[var(--header-height)] pt-4">
        <HeroHeader />
        <InstallationSection />
        <FeaturesSection sessionStrings={sessionStrings} />
        <QuickStartSection sessionString={sessionStrings[0]} />
        <DocumentationSection />
        <UsageExamplesSection sessionStrings={sessionStrings} />
        <OpenSourceSection />
        <Footer />
      </div>
    </>
  );
}
