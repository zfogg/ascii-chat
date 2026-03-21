import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../TrackedLink";

export default function InstallationSection() {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-teal-400 mb-4 sm:mb-6 border-b border-teal-900/50 pb-2"
      >
        📦 Installation
      </Heading>

      <div className="space-y-6">
        <div>
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3"
          >
            Pre-built static binaries
          </Heading>
          <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
            <p className="text-gray-300 mb-3">
              Download for <strong className="text-cyan-400">macOS</strong>,{" "}
              <strong className="text-purple-400">Linux</strong>, or{" "}
              <strong className="text-teal-400">Windows</strong>:
            </p>
            <TrackedLink
              href="https://github.com/zfogg/ascii-chat/releases/latest"
              label="Home - Download Latest Release"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-block bg-cyan-700 hover:bg-cyan-600 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
            >
              📦 Download Latest Release
            </TrackedLink>
          </div>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Homebrew
          </Heading>
          <CodeBlock language="bash">
            {`brew tap zfogg/ascii-chat
brew install ascii-chat`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Arch Linux (AUR)
          </Heading>
          <CodeBlock language="bash">
            {`paru -S ascii-chat
# or
yay -S ascii-chat`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-teal-300 mb-3"
          >
            Build from source
          </Heading>
          <CodeBlock language="bash">
            {`git clone https://github.com/zfogg/ascii-chat.git
cd ascii-chat

# Linux/macOS
./scripts/install-deps.sh
# Windows
./scripts/install-deps.ps1

make && sudo make install`}
          </CodeBlock>
        </div>
      </div>
    </section>
  );
}
