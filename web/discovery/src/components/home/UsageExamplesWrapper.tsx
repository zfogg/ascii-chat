import { Heading, UsageExamplesSection } from "@ascii-chat/shared/components";

export default function UsageExamplesWrapper({
  sessionStrings,
}: {
  sessionStrings: string[];
}) {
  return (
    <section className="mb-12">
      <Heading
        level={2}
        className="text-blue-400 border-b border-gray-700 pb-2 mb-4 text-2xl md:text-3xl"
      >
        💻 Usage Examples
      </Heading>
      <UsageExamplesSection
        sessionString={sessionStrings[1] || "jumbo-slayer-sergeant"}
        headingClassName="sr-only"
      />
    </section>
  );
}
