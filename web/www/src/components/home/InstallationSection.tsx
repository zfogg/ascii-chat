import { useState } from "react";
import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";
import TrackedLink from "../TrackedLink";

function InstallCommand({
  command,
  language,
}: {
  command: string;
  language: string;
}) {
  const [message, setMessage] = useState("Copy command");
  async function copy() {
    try {
      await navigator.clipboard.writeText(command);
      setMessage("Copied!");
    } catch {
      setMessage("Select the command to copy");
    }
  }
  return (
    <div>
      <CodeBlock language={language}>{command}</CodeBlock>
      <button
        type="button"
        className="mt-1 text-sm text-cyan-400 hover:text-cyan-200 underline cursor-pointer"
        aria-label={`Copy ${command}`}
        onClick={copy}
      >
        <span aria-live="polite">{message}</span>
      </button>
    </div>
  );
}

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
            Install the latest release
          </Heading>
          <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
            <p className="text-gray-300 mb-3">
              Ready-to-run binaries for{" "}
              <strong className="text-cyan-400">macOS</strong>,{" "}
              <strong className="text-purple-400">Linux</strong>, or{" "}
              <strong className="text-teal-400">Windows</strong>. The installer
              detects your system and downloads the matching x64 or ARM64
              release.
            </p>
            <div className="space-y-5 mb-6">
              <div>
                <Heading level={4} className="font-semibold text-cyan-300 mb-2">
                  macOS &amp; Linux · Bash
                </Heading>
                <InstallCommand
                  language="bash"
                  command="curl -fsSL https://ascii-chat.com/install.sh | bash"
                />
                <p className="text-sm text-gray-400 mt-2">
                  Installs into ~/.local. If needed, a Y/n prompt offers to add
                  it to your Bash and Zsh profiles. Press Enter to accept, then
                  open a new terminal. Existing PATH entries are left alone. For
                  everyone on this computer, install into /usr/local:
                </p>
                <InstallCommand
                  language="bash"
                  command="curl -fsSL https://ascii-chat.com/install.sh | sudo bash"
                />
              </div>
              <div>
                <Heading level={4} className="font-semibold text-teal-300 mb-2">
                  Windows · PowerShell
                </Heading>
                <InstallCommand
                  language="powershell"
                  command="irm https://ascii-chat.com/install.ps1 | iex"
                />
                <p className="text-sm text-gray-400 mt-2">
                  Run in PowerShell 5.1 or newer. Installs for your user and
                  adds ascii-chat to PATH. Run PowerShell as Administrator for a
                  system-wide install in Program Files.
                </p>
              </div>
              <p className="text-sm text-gray-400">
                Then run <code className="text-gray-200">ascii-chat</code>.
                Rerun to update; old installer-managed files and temporary
                downloads are removed. Terminal styling is powered by Gum. You
                can review the{" "}
                <a className="text-cyan-400 underline" href="/install.sh">
                  Bash script
                </a>{" "}
                or{" "}
                <a className="text-teal-400 underline" href="/install.ps1">
                  PowerShell script
                </a>{" "}
                first, or download an archive yourself below.
              </p>
            </div>
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
