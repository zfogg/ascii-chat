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
                  command={`# regular user install
curl -fsSL https://ascii-chat.com/install.sh | bash

# system-wide admin install
curl -fsSL https://ascii-chat.com/install.sh | sudo bash`}
                />
                <p className="text-sm text-gray-400 mt-2">
                  Installs into ~/.local for your user, or /usr/local with sudo.
                </p>
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
                downloads are removed. You can review the{" "}
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
            <Heading level={4} className="font-semibold text-cyan-300 mb-2">
              Download from GitHub
            </Heading>
            <p className="text-gray-300 mb-4">
              GitHub Releases has packages for Windows, macOS, and Linux:
              Windows MSI installers and ZIP archives, macOS PKG installers, and
              Linux DEB packages for Debian/Ubuntu and RPM packages for
              Fedora/RHEL. Linux and macOS tarballs (.tar.gz) are also available
              if you prefer to unpack the binaries yourself.
            </p>
            <TrackedLink
              href="https://github.com/zfogg/ascii-chat/releases/latest"
              label="Home - Download Latest Release"
              target="_blank"
              rel="noopener noreferrer"
              className="inline-flex items-center gap-2 bg-cyan-700 hover:bg-cyan-600 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
            >
              <svg
                aria-hidden="true"
                viewBox="0 0 24 24"
                fill="currentColor"
                className="w-5 h-5 shrink-0"
              >
                <path d="M12 .297C5.37.297 0 5.67 0 12.297c0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.043-1.61-4.043-1.61-.546-1.387-1.333-1.756-1.333-1.756-1.09-.745.083-.729.083-.729 1.205.084 1.838 1.237 1.838 1.237 1.07 1.835 2.809 1.305 3.495.998.108-.776.418-1.305.762-1.605-2.665-.3-5.466-1.333-5.466-5.93 0-1.31.469-2.38 1.236-3.22-.124-.303-.536-1.524.117-3.176 0 0 1.008-.322 3.301 1.23a11.52 11.52 0 0 1 3.003-.404c1.02.005 2.045.138 3.003.404 2.291-1.552 3.297-1.23 3.297-1.23.655 1.652.243 2.873.12 3.176.77.84 1.235 1.91 1.235 3.22 0 4.61-2.805 5.625-5.479 5.921.431.372.815 1.102.815 2.222 0 1.606-.015 2.898-.015 3.293 0 .322.216.694.825.576C20.565 22.092 24 17.595 24 12.297c0-6.627-5.373-12-12-12" />
              </svg>
              Download Latest Release
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
