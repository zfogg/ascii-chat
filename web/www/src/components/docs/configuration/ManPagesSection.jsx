import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function ManPagesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        📖 Man Pages
      </Heading>

      <p className="docs-paragraph">
        The complete reference documentation for ascii-chat is available as
        traditional Unix man pages:
      </p>

      <div className="space-y-3 mb-6">
        <div className="card-standard accent-teal">
          <Heading level={4} className="text-teal-300 font-semibold mb-2">
            <a
              href="/man1"
              className="text-teal-300 hover:text-teal-200 transition-colors"
            >
              ascii-chat(1)
            </a>{" "}
            - Command Reference
          </Heading>
          <p className="text-gray-400 text-sm">
            Complete list of all command-line options, environment variables,
            modes, examples, security details, and keyboard controls.
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading level={4} className="text-purple-300 font-semibold mb-2">
            <a
              href="/man5"
              className="text-purple-300 hover:text-purple-200 transition-colors"
            >
              ascii-chat(5)
            </a>{" "}
            - File Formats
          </Heading>
          <p className="text-gray-400 text-sm">
            Configuration file format (TOML), color scheme files, authentication
            files, and other data formats.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-4">
          View Man Pages
        </Heading>
        <p className="text-gray-400 text-sm mb-3">
          View ascii-chat documentation in your terminal:
        </p>
        <CodeBlock language="bash">
          {`man ascii-chat    # Command reference (section 1)
man 5 ascii-chat  # File formats (section 5)`}
        </CodeBlock>
        <p className="text-gray-400 text-sm mt-3 mb-3">
          Or view them on the web:
        </p>
        <CodeBlock language="bash">
          {`open https://ascii-chat.com/man1  # Command reference
open https://ascii-chat.com/man5  # File formats`}
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-4">
          Install Man Page for Terminal Access
        </Heading>

        <p className="text-gray-400 text-sm mb-4">
          By default, the ascii-chat man page is installed in the standard
          system location. If{" "}
          <code className="text-cyan-300">man ascii-chat</code> doesn't work,
          you may need to add the installation directory to your{" "}
          <code className="text-cyan-300">MANPATH</code>.
        </p>

        <div className="docs-subsection-spacing">
          <Heading level={4} className="text-teal-300 font-semibold mb-3">
            macOS (Homebrew)
          </Heading>
          <p className="text-gray-400 text-sm mb-2">
            If you installed ascii-chat via Homebrew, add the man page directory
            to your shell profile:
          </p>
          <CodeBlock language="bash">{`# Add to ~/.zshrc or ~/.bashrc
export MANPATH="$(brew --prefix)/share/man:$MANPATH"`}</CodeBlock>
          <p className="text-gray-400 text-sm mt-2 mb-3">
            Then reload your shell:
          </p>
          <CodeBlock language="bash">
            source ~/.zshrc # or source ~/.bashrc
          </CodeBlock>
        </div>

        <div className="docs-subsection-spacing">
          <Heading level={4} className="text-teal-300 font-semibold mb-3">
            Linux
          </Heading>
          <p className="text-gray-400 text-sm mb-2">
            The man page is typically installed to{" "}
            <code className="text-cyan-300">/usr/local/share/man</code> or{" "}
            <code className="text-cyan-300">/usr/share/man</code>. These are
            usually in your <code className="text-cyan-300">MANPATH</code> by
            default.
          </p>
          <p className="text-gray-400 text-sm">
            If not, add to your shell profile:
          </p>
          <CodeBlock language="bash">{`export MANPATH="/usr/local/share/man:$MANPATH"`}</CodeBlock>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-4">
          Generate Man Page Manually
        </Heading>
        <p className="text-gray-400 text-sm mb-3">
          Generate the man page from the binary:
        </p>
        <CodeBlock language="bash">
          {"ascii-chat --man-page-create > /tmp/ascii-chat.1"}
        </CodeBlock>
        <p className="text-gray-400 text-sm mt-3">Then view it with:</p>
        <CodeBlock language="bash">man /tmp/ascii-chat.1</CodeBlock>
      </div>

      <div className="info-box-info mt-4">
        <p className="text-gray-300 text-sm">
          <strong>📖 Complete Reference:</strong> The man page includes
          comprehensive sections on OPTIONS, ENVIRONMENT variables (all{" "}
          <code className="text-cyan-300">ASCII_CHAT_*</code> variables),
          EXAMPLES, SECURITY, KEYBOARD CONTROLS, and more.
        </p>
      </div>
    </section>
  );
}
