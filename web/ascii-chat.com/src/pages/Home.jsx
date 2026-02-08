import Footer from "../components/Footer";
import TrackedLink from "../components/TrackedLink";
import { PreCode } from "@ascii-chat/shared/components";

export default function Home() {
  return (
    <div className="bg-gray-950 text-gray-100 flex flex-col">
      <div className="flex-1 flex flex-col max-w-4xl mx-auto px-4 sm:px-6 py-8 sm:py-12 w-full">
        {/* Header */}
        <header className="mb-12 sm:mb-16 text-center">
          <h1 className="text-4xl sm:text-5xl md:text-6xl font-bold mb-4">
            <span className="text-cyan-400">💻</span>
            <span className="text-purple-400">📸</span>{" "}
            <span className="text-cyan-400">ascii</span>
            <span className="text-gray-500">-</span>
            <span className="text-teal-400">chat</span>{" "}
            <span className="text-pink-400">🔡</span>
            <span className="text-purple-400">💬</span>
          </h1>
          <p className="text-lg sm:text-xl md:text-2xl text-gray-300 mb-2">
            Video chat in your terminal
          </p>
          <p className="text-sm sm:text-base text-gray-400">
            Real-time terminal-based video conferencing with ASCII art
            rendering, end-to-end encryption, and audio support
          </p>
        </header>

        {/* Installation */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-teal-400 mb-4 sm:mb-6 border-b border-teal-900/50 pb-2">
            📦 Installation
          </h2>

          <div className="space-y-6">
            <div>
              <h3 className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3">
                Pre-built static binaries (all platforms)
              </h3>
              <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
                <p className="text-gray-300 mb-3">
                  Download pre-built static binaries for{" "}
                  <strong className="text-cyan-400">macOS</strong>,{" "}
                  <strong className="text-purple-400">Linux</strong>, and{" "}
                  <strong className="text-teal-400">Windows</strong>:
                </p>
                <TrackedLink
                  href="https://github.com/zfogg/ascii-chat/releases/latest"
                  label="Home - Download Latest Release"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded-lg transition-colors"
                >
                  📦 Download Latest Release
                </TrackedLink>
              </div>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Homebrew
              </h3>
              <PreCode>{`brew tap zfogg/ascii-chat
brew install ascii-chat`}</PreCode>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-pink-300 mb-3">
                Arch Linux (AUR)
              </h3>
              <PreCode>{`paru -S ascii-chat
# or
yay -S ascii-chat`}</PreCode>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-teal-300 mb-3">
                Build from source
              </h3>
              <PreCode>{`git clone https://github.com/zfogg/ascii-chat.git
cd ascii-chat

# Linux/macOS
./scripts/install-deps.sh

# Windows
./scripts/install-deps.ps1

cmake --preset default && cmake --build build
./build/bin/ascii-chat`}</PreCode>
            </div>
          </div>
        </section>

        {/* Features */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-purple-400 mb-4 sm:mb-6 border-b border-purple-900/50 pb-2">
            ✨ Features
          </h2>

          <div className="grid sm:grid-cols-2 gap-4 sm:gap-6">
            <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3">
                📺 Terminal Video calls
              </h3>
              <p className="text-gray-300">
                Webcam video over tcp/ip rendered as ASCII art in real-time.
                Works in any terminal-rxvt-unicode, iTerm, Kitty, even SSH
                sessions.
              </p>
            </div>

            <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-purple-300 mb-3">
                🔒 End-to-End Encryption
              </h3>
              <p className="text-gray-300">
                Ed25519 authentication with X25519 key exchange. Your video and
                audio never leave the secure tunnel between peers. SSH and GPG
                key supported. GitHub and GitLab integrations.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-teal-300 mb-3">
                🎤 Voice Chat
              </h3>
              <p className="text-gray-300">
                Real-time audio with Opus encoding. Talk while you see each
                other's ASCII faces. WebRTC AEC3 echo cancellation integration.
                Multi-client audio mixing supported.
              </p>
            </div>

            <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-pink-300 mb-3">
                🌍 Zero Config Networking
              </h3>
              <p className="text-gray-300">
                Share a memorable three-word string like{" "}
                <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
                  happy-sunset-ocean
                </code>{" "}
                that users can connect with connection and NAT traversal is
                transparently handled. Support for UPnP, WebRTC, and mDNS.
              </p>
            </div>

            <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3">
                👥 3+ Person Conference Calls
              </h3>
              <p className="text-gray-300">
                3+ people can join the same session. Video grid layout
                automatically adjusts. Like Zoom or Google Hangouts, but in your
                terminal.
              </p>
            </div>

            <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
              <h3 className="text-lg sm:text-xl font-semibold text-purple-300 mb-3">
                🎨 Customizable Rendering
              </h3>
              <p className="text-gray-300">
                Choose ASCII palettes, color modes (mono/16/256/truecolor), and
                rendering styles and modes.
              </p>
            </div>
          </div>
        </section>

        {/* Quick Start */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2">
            ⚡ Quick Start
          </h2>

          <div className="space-y-6 sm:space-y-8">
            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Start a server
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  {`# Register with ACDS and get a session string
ascii-chat server`}
                  <span className="text-gray-500">{`
# Session: happy-sunset-ocean`}</span>
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Join a session
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  {`# Connect using the session string
ascii-chat happy-sunset-ocean`}
                  <span className="text-gray-500">{`
# That's it! No configuration needed.`}</span>
                </code>
              </pre>
            </div>
          </div>
        </section>

        {/* Links */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2">
            📚 Documentation
          </h2>

          <div className="grid sm:grid-cols-2 gap-4">
            <TrackedLink
              to="/docs"
              label="Home - Docs Hub"
              className="bg-gray-900/50 border border-teal-900/50 rounded-lg p-4 hover:border-teal-500/50 transition-colors"
            >
              <h3 className="text-teal-300 font-semibold mb-1">📚 Docs Hub</h3>
              <p className="text-gray-400 text-sm">
                Configuration, hardware, terminal, snapshot, network, media
              </p>
            </TrackedLink>

            <TrackedLink
              to="/man1"
              label="Home - Docs Man Page (1)"
              className="bg-gray-900/50 border border-cyan-900/50 rounded-lg p-4 hover:border-cyan-500/50 transition-colors"
            >
              <h3 className="text-cyan-300 font-semibold mb-1">
                📖 ascii-chat(1)
              </h3>
              <p className="text-gray-400 text-sm">
                Complete command-line reference
              </p>
            </TrackedLink>

            <TrackedLink
              to="/man5"
              label="Home - Docs Man Page (5)"
              className="bg-gray-900/50 border border-orange-900/50 rounded-lg p-4 hover:border-orange-500/50 transition-colors"
            >
              <h3 className="text-orange-300 font-semibold mb-1">
                📋 ascii-chat(5)
              </h3>
              <p className="text-gray-400 text-sm">
                File formats and configuration
              </p>
            </TrackedLink>

            <TrackedLink
              to="/crypto"
              label="Home - Docs Cryptography"
              className="bg-gray-900/50 border border-purple-900/50 rounded-lg p-4 hover:border-purple-500/50 transition-colors"
            >
              <h3 className="text-purple-300 font-semibold mb-1">
                🔐 Cryptography
              </h3>
              <p className="text-gray-400 text-sm">
                Encryption, keys, and authentication
              </p>
            </TrackedLink>

            <TrackedLink
              to="/man1#ENVIRONMENT"
              label="Home - Docs Environment Variables"
              className="bg-gray-900/50 border border-green-900/50 rounded-lg p-4 hover:border-green-500/50 transition-colors"
            >
              <h3 className="text-green-300 font-semibold mb-1">
                🌍 Environment Variables
              </h3>
              <p className="text-gray-400 text-sm">
                See man page ENVIRONMENT section
              </p>
            </TrackedLink>

            <TrackedLink
              href="https://zfogg.github.io/ascii-chat/"
              label="Home - Docs API"
              target="_blank"
              rel="noopener noreferrer"
              className="bg-gray-900/50 border border-teal-900/50 rounded-lg p-4 hover:border-teal-500/50 transition-colors"
            >
              <h3 className="text-teal-300 font-semibold mb-1">
                📘 API Documentation
              </h3>
              <p className="text-gray-400 text-sm">
                Full Doxygen reference for developers
              </p>
            </TrackedLink>

            <TrackedLink
              href="https://discovery.ascii-chat.com"
              label="Home - Docs ACDS"
              target="_blank"
              rel="noopener noreferrer"
              className="bg-gray-900/50 border border-pink-900/50 rounded-lg p-4 hover:border-pink-500/50 transition-colors"
            >
              <h3 className="text-pink-300 font-semibold mb-1">
                🔍 Discovery Service
              </h3>
              <p className="text-gray-400 text-sm">
                ACDS public keys and details
              </p>
            </TrackedLink>
          </div>
        </section>

        {/* Examples */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-pink-400 mb-4 sm:mb-6 border-b border-pink-900/50 pb-2">
            💻 Usage Examples
          </h2>

          <div className="bg-purple-900/20 border border-purple-700/50 rounded-lg p-4 mb-6">
            <p className="text-gray-300 text-sm">
              💡 For complete documentation of all command-line flags and
              configuration file options, see the{" "}
              <TrackedLink
                to="/man1"
                label="Home - Man Page Reference"
                className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
              >
                man page
              </TrackedLink>
              .
            </p>
          </div>

          <div className="space-y-8">
            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Getting help
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# View built-in help
`}</span>
                  {`ascii-chat --help

`}
                  <span className="text-gray-500">{`# Read the full man page
`}</span>
                  {`man ascii-chat

`}
                  <span className="text-gray-500">{`# Get help for specific modes
`}</span>
                  {`ascii-chat <mode> --help`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Local connection (no ACDS)
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server binds to localhost
`}</span>
                  {`ascii-chat server

`}
                  <span className="text-gray-500">{`# Client connects to localhost
`}</span>
                  {`ascii-chat client`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Internet session with ACDS
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server registers with official ACDS
`}</span>
                  {`ascii-chat server
`}
                  <span className="text-gray-500">{`# Session: happy-sunset-ocean

`}</span>
                  <span className="text-gray-500">{`# Client looks up session and connects automatically
`}</span>
                  {`ascii-chat happy-sunset-ocean`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-teal-300 mb-3">
                Authenticated session with SSH keys
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server with Ed25519 key
`}</span>
                  {`ascii-chat server --key ~/.ssh/id_ed25519

`}
                  <span className="text-gray-500">{`# Client authenticates with their key
`}</span>
                  {`ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Server whitelisting clients with GitHub SSH keys
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server whitelists GitHub user's SSH keys
`}</span>
                  {`ascii-chat server --key ~/.ssh/id_ed25519 --client-keys github:zfogg

`}
                  <span className="text-gray-500">{`# Only clients with those keys can connect
`}</span>
                  {`ascii-chat happy-sunset-ocean --key ~/.ssh/id_ed25519`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Client whitelisting server with GitHub GPG keys
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Server with GPG key
`}</span>
                  {`ascii-chat server --key gpg:897607FA43DC66F6

`}
                  <span className="text-gray-500">{`# Client verifies server against GitHub GPG keys
`}</span>
                  {`ascii-chat happy-sunset-ocean --server-key github:zfogg.gpg`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-pink-300 mb-3">
                Mirror mode (test webcam locally)
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# View your webcam as ASCII without connecting anywhere
`}</span>
                  {`ascii-chat mirror --palette blocks`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Rainbow matrix effect
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Digital rain effect with rainbow colors
`}</span>
                  {`ascii-chat mirror --color-filter rainbow --matrix`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Capture ASCII selfie to file
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Take a snapshot from your webcam and save to file
`}</span>
                  {`ascii-chat --color mirror --snapshot --render-mode half-block > selfie.txt`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Stream video file as ASCII art
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Play MP4 video as ASCII (also works with MOV, AVI, MKV, WebM, GIF, ...)
`}</span>
                  {`ascii-chat mirror --file video.mp4`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-teal-300 mb-3">
                Stream YouTube as ASCII art
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Watch YouTube video as ASCII art locally (mirror mode)
`}</span>
                  {`ascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' -s 38:29 --color-mode truecolor
`}
                  <span className="text-gray-500">{`
# Share YouTube video with others in a call (client mode)
`}</span>
                  {`ascii-chat happy-sunset-ocean --url 'https://youtu.be/7ynHVGCehoM' -s 38:29`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-teal-300 mb-3">
                Convert video to ASCII and preview
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Convert video frame to ASCII and preview first 30 lines
`}</span>
                  {`ascii-chat mirror --file video.mp4 --snapshot | head -30`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-purple-300 mb-3">
                Play animated GIF
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Loop an animated GIF continuously
`}</span>
                  {`ascii-chat mirror --file animation.gif --loop`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-cyan-300 mb-3">
                Convert image to ASCII
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Display JPEG or PNG image as ASCII art
`}</span>
                  {`ascii-chat mirror --file photo.jpg --snapshot`}
                </code>
              </pre>
            </div>

            <div>
              <h3 className="text-xl font-semibold text-pink-300 mb-3">
                Pipe video through stdin
              </h3>
              <pre className="bg-gray-900  rounded-lg p-4 overflow-x-auto">
                <code className="text-teal-300">
                  <span className="text-gray-500">{`# Stream from stdin (useful for chaining commands)
`}</span>
                  {`cat video.mp4 | ascii-chat mirror --file -`}
                </code>
              </pre>
            </div>
          </div>
        </section>

        {/* Open Source */}
        <section className="mb-12 sm:mb-16">
          <h2 className="text-2xl sm:text-3xl font-bold text-cyan-400 mb-4 sm:mb-6 border-b border-cyan-900/50 pb-2">
            💝 Open Source
          </h2>

          <div className="space-y-6">
            <div className="bg-gray-900/50  rounded-lg p-6">
              <p className="text-gray-300 mb-4">
                <strong className="text-cyan-400">ascii-chat</strong> and{" "}
                <strong className="text-purple-400">libasciichat</strong> are
                free and open source under the{" "}
                <strong className="text-teal-400">MIT License</strong>.
              </p>
              <p className="text-gray-300 mb-4">
                Cross-platform:{" "}
                <strong className="text-cyan-400">Windows</strong>,{" "}
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
              <h3 className="text-purple-300 font-semibold text-xl mb-4">
                Build with libasciichat
              </h3>
              <p className="text-gray-300 mb-4">
                <strong className="text-purple-400">libasciichat</strong> is the
                core library. It implements{" "}
                <strong className="text-pink-400">ACIP</strong> (ASCII-Chat
                Internet Protocol) for encrypted peer-to-peer video and audio
                streaming.
              </p>

              <ul className="list-disc list-inside space-y-2 text-gray-300 mb-4 ml-4">
                <li>
                  <strong className="text-cyan-400">Video:</strong> Webcam,
                  files, ASCII conversion, palettes, color modes
                </li>
                <li>
                  <strong className="text-purple-400">Audio:</strong> Opus
                  codec, WebRTC AEC3 echo cancellation
                </li>
                <li>
                  <strong className="text-teal-400">Crypto:</strong> Ed25519,
                  X25519, XSalsa20-Poly1305
                </li>
                <li>
                  <strong className="text-pink-400">Network:</strong> TCP, NAT
                  traversal, WebRTC ICE, ACDS
                </li>
                <li>
                  <strong className="text-cyan-400">Protocol:</strong>{" "}
                  Handshake, sessions, packets, multi-client
                </li>
              </ul>

              <p className="text-gray-300 mb-4">
                Ships with headers, CMake configs, and a pkg-config file.
              </p>

              <div className="bg-gray-950/50  rounded-lg p-4">
                <h4 className="text-cyan-300 font-semibold mb-3">
                  Installing libasciichat
                </h4>
                <div className="space-y-3">
                  <div>
                    <p className="text-gray-400 text-sm mb-2">Homebrew:</p>
                    <pre className="bg-gray-900  rounded p-3">
                      <code className="text-teal-300">{`brew install zfogg/ascii-chat/ascii-chat  # includes libasciichat`}</code>
                    </pre>
                  </div>
                  <div>
                    <p className="text-gray-400 text-sm mb-2">
                      Arch Linux (AUR):
                    </p>
                    <pre className="bg-gray-900  rounded p-3">
                      <code className="text-teal-300">{`paru -S libasciichat
# or for latest git version
paru -S libasciichat-git`}</code>
                    </pre>
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
              <h3 className="text-teal-300 font-semibold text-xl mb-4">
                Using libasciichat in Your Project
              </h3>

              <div className="space-y-4">
                <div>
                  <h4 className="text-cyan-400 font-semibold mb-2">
                    With pkg-config:
                  </h4>
                  <pre className="bg-gray-900  rounded-lg p-3 overflow-x-auto">
                    <code className="text-teal-300">{`gcc myapp.c $(pkg-config --cflags --libs libasciichat) -o myapp`}</code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-purple-400 font-semibold mb-2">
                    With CMake:
                  </h4>
                  <pre className="bg-gray-900  rounded-lg p-3 overflow-x-auto">
                    <code className="text-teal-300">{`find_package(libasciichat REQUIRED)
target_link_libraries(myapp libasciichat::libasciichat)`}</code>
                  </pre>
                </div>

                <div>
                  <h4 className="text-pink-400 font-semibold mb-2">
                    Include headers:
                  </h4>
                  <pre className="bg-gray-900  rounded-lg p-3 overflow-x-auto">
                    <code className="text-teal-300">{`#include <ascii-chat/video/ascii.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/crypto/crypto.h>`}</code>
                  </pre>
                </div>
              </div>
            </div>
          </div>
        </section>

        {/* Footer */}
        <Footer />
      </div>
    </div>
  );
}
