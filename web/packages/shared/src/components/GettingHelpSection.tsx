import { CodeBlock } from "./CodeBlock";
import { Heading } from "./index";

export interface GettingHelpSectionProps {
  /**
   * The binary/mode name for help. Default: "<mode>"
   * Examples: "<mode>" (ascii-chat.com), "discovery-service" (discovery.ascii-chat.com)
   */
  modeExample?: string;
  /**
   * Optional custom heading text. Default: "Getting help"
   */
  headingText?: string;
  /**
   * Optional custom class names for the heading
   */
  headingClassName?: string;
}

export function GettingHelpSection({
  modeExample = "<mode>",
  headingText = "Getting help",
  headingClassName = "text-xl font-semibold text-cyan-300 mb-3",
}: GettingHelpSectionProps) {
  const completionsCmd =
    modeExample === "<mode>"
      ? `source <(ascii-chat --completions zsh 2>/dev/null)
# Then: ascii-chat <tab> | ascii-chat --<tab> | ascii-chat server --<tab>`
      : `source <(ascii-chat --completions zsh 2>/dev/null)`;

  const modeHelpCmd =
    modeExample === "<mode>"
      ? `ascii-chat <mode> --help`
      : `ascii-chat ${modeExample} --help`;

  return (
    <div>
      <Heading level={3} className={headingClassName}>
        {headingText}
      </Heading>
      <CodeBlock language="bash">
        {`# View built-in help
ascii-chat --help

# Read the full man page
man ascii-chat

# Get help for specific modes
${modeHelpCmd}

# Enable shell completions (bash, fish, zsh, and powershell available)
${completionsCmd}`}
      </CodeBlock>
    </div>
  );
}
