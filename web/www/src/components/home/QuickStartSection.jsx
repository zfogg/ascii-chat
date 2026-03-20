import { Heading, UsageExamplesSection } from "@ascii-chat/shared/components";

export default function QuickStartSection({ sessionString }) {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2"
      >
        ⚡ Quick Start
      </Heading>

      <UsageExamplesSection
        sessionString={sessionString}
        headingClassName="sr-only"
      />
    </section>
  );
}
