import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../TrackedLink";

export default function OpenSourceSection() {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2"
      >
        💝 Open Source
      </Heading>

      <div className="space-y-6">
        <div className="bg-gray-900/50  rounded-lg p-6">
          <p className="text-gray-300 mb-4">
            <strong className="text-cyan-400">ascii-chat</strong> and{" "}
            <strong className="text-purple-400">libasciichat</strong> are free
            and open source under the{" "}
            <strong className="text-teal-400">MIT License</strong>.
          </p>
          <p className="text-gray-300 mb-4">
            Cross-platform: <strong className="text-cyan-400">Windows</strong>,{" "}
            <strong className="text-purple-400">Linux</strong>, and{" "}
            <strong className="text-teal-400">macOS</strong> on both{" "}
            <strong className="text-pink-400">x86_64</strong> and{" "}
            <strong className="text-cyan-400">ARM64</strong>.
          </p>
          <p className="text-gray-300">
            Contributions welcome. Fork it and send pull requests.
          </p>
        </div>

        <div className="bg-gray-900/50  rounded-lg p-6">
          <Heading
            level={3}
            className="text-purple-300 font-semibold text-xl mb-4"
          >
            Build with libasciichat
          </Heading>
          <p className="text-gray-300 mb-4">
            <strong className="text-purple-400">libasciichat</strong> is the
            core library. It implements{" "}
            <strong className="text-pink-400">ACIP</strong> (ASCII-Chat Internet
            Protocol) for encrypted peer-to-peer video and audio streaming.{" "}
            <TrackedLink
              to="/man3"
              label="Home - libasciichat API Documentation"
              className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
            >
              View man(3) API docs →
            </TrackedLink>
          </p>

          <ul className="list-disc list-inside space-y-2 text-gray-300 mb-4 ml-4">
            <li>
              <strong className="text-cyan-400">Video:</strong> Webcam, files,
              ASCII conversion, palettes, color modes, H.265 encoding
            </li>
            <li>
              <strong className="text-purple-400">Audio:</strong> Opus codec,
              WebRTC AEC3 echo cancellation
            </li>
            <li>
              <strong className="text-teal-400">Crypto:</strong> Ed25519,
              X25519, XSalsa20-Poly1305, Argon2
            </li>
            <li>
              <strong className="text-pink-400">Network:</strong> TCP, NAT
              traversal, WebRTC ICE, ACDS
            </li>
            <li>
              <strong className="text-cyan-400">Protocol:</strong> Handshake,
              sessions, packets, multi-client
            </li>
            <li>
              <strong className="text-purple-400">Terminal:</strong> Capability
              detection, color modes, UTF-8, cross-platform
            </li>
          </ul>

          <p className="text-gray-300 mb-4">
            Ships with headers, CMake configs, and a pkg-config file.
          </p>

          <div className="bg-gray-950/50  rounded-lg p-4">
            <Heading level={4} className="text-cyan-300 font-semibold mb-3">
              Installing libasciichat
            </Heading>
            <div className="space-y-3">
              <div>
                <p className="text-gray-400 text-sm mb-2">Homebrew:</p>
                <CodeBlock language="bash">
                  {`brew install zfogg/ascii-chat/ascii-chat  # includes libasciichat`}
                </CodeBlock>
              </div>
              <div>
                <p className="text-gray-400 text-sm mb-2">Arch Linux (AUR):</p>
                <CodeBlock language="bash">
                  {`paru -S libasciichat
# or for latest git version
paru -S libasciichat-git`}
                </CodeBlock>
              </div>
              <div>
                <p className="text-gray-400 text-sm mb-2">
                  From GitHub releases:
                </p>
                <p className="text-gray-300 text-sm">
                  Download and install a libasciichat package from{" "}
                  <TrackedLink
                    href="https://github.com/zfogg/ascii-chat/releases/latest"
                    label="Home - libasciichat releases"
                    target="_blank"
                    rel="noopener noreferrer"
                    className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
                  >
                    GitHub releases
                  </TrackedLink>
                </p>
              </div>
            </div>
          </div>
        </div>

        <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-6">
          <Heading
            level={3}
            className="text-teal-300 font-semibold text-xl mb-4"
          >
            Using libasciichat in Your Project
          </Heading>

          <div className="space-y-4">
            <div>
              <Heading level={4} className="text-cyan-400 font-semibold mb-2">
                With pkg-config:
              </Heading>
              <CodeBlock language="bash">
                {`clang myapp.c $(pkg-config --cflags --libs libasciichat) -o myapp`}
              </CodeBlock>
            </div>

            <div>
              <Heading level={4} className="text-purple-400 font-semibold mb-2">
                With CMake:
              </Heading>
              <CodeBlock language="bash">
                {`find_package(libasciichat REQUIRED)
target_link_libraries(myapp libasciichat::libasciichat)`}
              </CodeBlock>
            </div>

            <div>
              <Heading level={4} className="text-pink-400 font-semibold mb-2">
                Include headers:
              </Heading>
              <CodeBlock language="bash">
                {`#include <ascii-chat/video/ascii/ascii.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/crypto/crypto.h>`}
              </CodeBlock>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}
