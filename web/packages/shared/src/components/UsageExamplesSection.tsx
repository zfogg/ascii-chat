import { CodeBlock } from "./CodeBlock";
import { Heading } from "./index";

export interface UsageExamplesSectionProps {
  /**
   * Session string to use in examples. Required.
   */
  sessionString: string;
  /**
   * Optional custom heading text. Default: "Usage Examples"
   */
  headingText?: string;
  /**
   * Optional custom class names for the heading
   */
  headingClassName?: string;
}

export function UsageExamplesSection({
  sessionString,
  headingText = "Usage Examples",
  headingClassName = "text-xl font-semibold text-purple-300 mb-3",
}: UsageExamplesSectionProps) {
  return (
    <div>
      <Heading level={3} className={headingClassName}>
        {headingText}
      </Heading>
      <CodeBlock language="bash">
        {`# Start a new session
ascii-chat
# It logs "Session string: ${sessionString}"

# Copy the session string it outputs and share it with a friend securely

# Join a session using the session string
ascii-chat ${sessionString}`}
      </CodeBlock>
    </div>
  );
}
