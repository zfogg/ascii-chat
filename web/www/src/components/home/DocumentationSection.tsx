import { Heading } from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";
import TrackedLink from "../TrackedLink";

export default function DocumentationSection() {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2"
      >
        📚 Documentation
      </Heading>

      <div className="text-gray-300 space-y-4 text-sm sm:text-base leading-relaxed">
        <p>
          All ascii-chat documentation is available both in your terminal and
          online. If you have ascii-chat installed, run{" "}
          <code className="text-cyan-300 bg-gray-800 px-1.5 py-0.5 rounded text-xs">
            ascii-chat --help
          </code>{" "}
          for a quick reference, or browse the full{" "}
          <TrackedLink
            to="/docs"
            label="Home - Documentation"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            /docs
          </TrackedLink>{" "}
          covering configuration, networking, media streaming, terminal
          compatibility, and more.
        </p>

        <p>
          The{" "}
          <TrackedLink
            to="/man1"
            label="Home - Man Page (1)"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            ascii-chat(1)
          </TrackedLink>{" "}
          man page is the complete command-line reference — every flag, mode,
          and environment variable. The{" "}
          <TrackedLink
            to="/man5"
            label="Home - Man Page (5)"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            ascii-chat(5)
          </TrackedLink>{" "}
          page covers the configuration file format. Both are the same pages you
          get from{" "}
          <code className="text-cyan-300 bg-gray-800 px-1.5 py-0.5 rounded text-xs">
            man 1 ascii-chat
          </code>{" "}
          and{" "}
          <code className="text-cyan-300 bg-gray-800 px-1.5 py-0.5 rounded text-xs">
            man 5 ascii-chat
          </code>{" "}
          in your terminal.
        </p>

        <p>
          For developers working with the source or building against
          libasciichat, the{" "}
          <TrackedLink
            to="/man3"
            label="Home - Man Page (3)"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            ascii-chat-*(3)
          </TrackedLink>{" "}
          pages document every struct, function, and header file in the
          codebase. These are generated from the source with Doxygen and
          searchable online. The full{" "}
          <TrackedLink
            href="https://zfogg.github.io/ascii-chat/"
            label="Home - Doxygen"
            target="_blank"
            rel="noopener noreferrer"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            Doxygen HTML
          </TrackedLink>{" "}
          is also hosted on GitHub Pages.
        </p>

        <p>
          The{" "}
          <TrackedLink
            href={SITES.DISCOVERY}
            label="Home - Discovery Service"
            target="_blank"
            rel="noopener noreferrer"
            className="text-cyan-400 hover:text-cyan-300 underline"
          >
            discovery service
          </TrackedLink>{" "}
          has its own page with public keys and connection details.
        </p>
      </div>
    </section>
  );
}
