import { Heading } from "@ascii-chat/shared/components";

export default function FeaturesSection({
  sessionStrings,
}: {
  sessionStrings: string[];
}) {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-purple-400 mb-4 sm:mb-6 border-b border-purple-900/50 pb-2"
      >
        ✨ Features
      </Heading>

      <div className="grid sm:grid-cols-2 gap-4 sm:gap-6">
        <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3"
          >
            📺 Terminal Video calls
          </Heading>
          <p className="text-gray-300">
            Webcam video over tcp/ip rendered as ASCII art in real-time. Works
            in any terminal-rxvt-unicode, iTerm, Kitty, even SSH sessions.
          </p>
        </div>

        <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-purple-300 mb-3"
          >
            🔒 End-to-End Encryption
          </Heading>
          <p className="text-gray-300">
            Ed25519 authentication with X25519 key exchange. Your video and
            audio never leave the secure tunnel between peers. SSH and GPG key
            supported. GitHub and GitLab integrations.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-teal-900/30 rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-teal-300 mb-3"
          >
            🎤 Voice Chat
          </Heading>
          <p className="text-gray-300">
            Real-time audio with Opus encoding. Talk while you see each other's
            ASCII faces. WebRTC AEC3 echo cancellation integration. Multi-client
            audio mixing supported.
          </p>
        </div>

        <div className="bg-gray-900/50 border border-pink-900/30 rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-pink-300 mb-3"
          >
            🌍 Zero Config Networking
          </Heading>
          <p className="text-gray-300">
            Share a memorable three-word string like{" "}
            <code className="text-pink-400 bg-gray-950 px-2 py-1 rounded">
              {sessionStrings[1]}
            </code>{" "}
            that users can connect with connection and NAT traversal is
            transparently handled. Support for UPnP, WebRTC, and mDNS.
          </p>
        </div>

        <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-cyan-300 mb-3"
          >
            👥 3+ Person Conference Calls
          </Heading>
          <p className="text-gray-300">
            3+ people can join the same session. Video grid layout automatically
            adjusts. Like Zoom or Google Hangouts, but in your terminal.
          </p>
        </div>

        <div className="bg-gray-900/50  rounded-lg p-4 sm:p-6">
          <Heading
            level={3}
            className="text-lg sm:text-xl font-semibold text-purple-300 mb-3"
          >
            🎨 Customizable Rendering
          </Heading>
          <p className="text-gray-300">
            Choose ASCII palettes, color modes (mono/16/256/truecolor), and
            rendering styles and modes.
          </p>
        </div>
      </div>
    </section>
  );
}
