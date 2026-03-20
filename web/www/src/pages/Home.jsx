import { useEffect, useState } from "react";
import { fetchSessionStrings } from "@ascii-chat/shared/utils";
import { useAnchorNavigation } from "../hooks/useAnchorNavigation";
import { AsciiChatHead } from "../components/AsciiChatHead";
import Footer from "../components/Footer";
import HeroHeader from "../components/home/HeroHeader";
import InstallationSection from "../components/home/InstallationSection";
import FeaturesSection from "../components/home/FeaturesSection";
import QuickStartSection from "../components/home/QuickStartSection";
import DocumentationSection from "../components/home/DocumentationSection";
import UsageExamplesSection from "../components/home/UsageExamplesSection";
import OpenSourceSection from "../components/home/OpenSourceSection";

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
