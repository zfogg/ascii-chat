import { GettingHelpSection, Heading } from "@ascii-chat/shared/components";

export default function GettingHelpWrapper() {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        📖 Getting Help
      </Heading>
      <p className="leading-relaxed mb-4 text-base md:text-lg">
        For complete documentation and options, use the built-in help system:
      </p>
      <GettingHelpSection
        modeExample="discovery-service"
        headingClassName="sr-only"
      />
    </section>
  );
}
