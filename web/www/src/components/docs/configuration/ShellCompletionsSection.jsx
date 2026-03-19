import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ShellCompletionsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        🐚 Shell Completions
      </Heading>
      <p className="docs-paragraph">
        Generate shell completions for bash, fish, or zsh:
      </p>
      <CodeBlock language="bash">
        {
          "ascii-chat --completions bash > ~/.bash_completion.d/ascii-chat\nascii-chat --completions fish > ~/.config/fish/completions/ascii-chat.fish\nascii-chat --completions zsh > ~/.zfunc/_ascii-chat"
        }
      </CodeBlock>
    </section>
  );
}
