import { GettingHelpSection, Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../TrackedLink";

export default function UsageExamplesSection({ sessionStrings }) {
  return (
    <section className="mb-12 sm:mb-16">
      <Heading
        level={2}
        className="text-2xl sm:text-3xl font-bold text-pink-400 mb-4 sm:mb-6 border-b border-pink-900/50 pb-2"
      >
        💻 Usage Examples
      </Heading>

      <div className="bg-purple-900/20 border border-purple-700/50 rounded-lg p-4 mb-6">
        <p className="text-gray-300 text-sm">
          💡 For complete documentation of all command-line flags and
          configuration file options, see the{" "}
          <TrackedLink
            to="/man1"
            label="Home - Man Page Reference"
            className="text-cyan-400 hover:text-cyan-300 transition-colors underline"
          >
            man(1) page
          </TrackedLink>
          .
        </p>
      </div>

      <div className="space-y-8">
        <div>
          <GettingHelpSection
            modeExample="<mode>"
            introText=""
            headingClassName="text-xl font-semibold text-cyan-300 mb-3"
          />
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Local connection (no ACDS)
          </Heading>
          <CodeBlock language="bash">
            {`# Server binds to localhost
ascii-chat server

# Client connects to localhost
ascii-chat client`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-yellow-300 mb-3"
          >
            Internet session with ACDS
          </Heading>
          <CodeBlock language="bash">
            {`# Server registers with official ACDS
ascii-chat server
# Session: ${sessionStrings[1]}

# Client looks up session and connects automatically
ascii-chat ${sessionStrings[1]}`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-teal-300 mb-3"
          >
            Authenticated session with SSH keys
          </Heading>
          <CodeBlock language="bash">
            {`# Server with Ed25519 key
ascii-chat server --key ~/.ssh/id_ed25519

# Client authenticates with their key
ascii-chat ${sessionStrings[2]} --key ~/.ssh/id_ed25519`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Server whitelisting clients with GitHub SSH keys
          </Heading>
          <CodeBlock language="bash">
            {`# Server whitelists GitHub user's SSH keys
ascii-chat server --key ~/.ssh/id_ed25519 --client-keys github:zfogg

# Only clients with those keys can connect
ascii-chat ${sessionStrings[3]} --key ~/.ssh/id_ed25519`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Client whitelisting server with GitHub GPG keys
          </Heading>
          <CodeBlock language="bash">
            {`# Server with GPG key
ascii-chat server --key gpg:897607FA43DC66F6

# Client verifies server against GitHub GPG keys
ascii-chat ${sessionStrings[4]} --server-key github:zfogg.gpg`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Mirror mode (test webcam locally)
          </Heading>
          <CodeBlock language="bash">
            {`# View your webcam as ASCII without connecting anywhere
ascii-chat mirror --palette blocks`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Rainbow matrix effect
          </Heading>
          <CodeBlock language="bash">
            {`# Digital rain effect with rainbow colors
ascii-chat mirror --color-filter rainbow --matrix`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Capture ASCII selfie to file
          </Heading>
          <CodeBlock language="bash">
            {`# Take a snapshot from your webcam and save to file
ascii-chat --snapshot-delay 0 --color mirror --snapshot --render-mode half-block > selfie.txt
cat selfie.txt`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-yellow-300 mb-3"
          >
            Stream video file as ASCII art
          </Heading>
          <CodeBlock language="bash">
            {`# Play MP4 video as ASCII (also works with MOV, AVI, MKV, WebM, GIF, ...)
ascii-chat mirror --file video.mp4`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-teal-300 mb-3"
          >
            Stream YouTube as ASCII art
          </Heading>
          <CodeBlock language="bash">
            {`# Watch YouTube video as ASCII art locally (mirror mode)
ascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' -s 38:29 --color-mode truecolor

# Share YouTube video with others in a call
ascii-chat ${sessionStrings[5]} --url 'https://youtu.be/7ynHVGCehoM' -s 38:29`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Watch Twitch stream as ASCII art
          </Heading>
          <CodeBlock language="bash">
            {`# Watch Twitch stream locally in ASCII (mirror mode)
ascii-chat mirror --url 'https://www.twitch.tv/ludwig'

# Share Twitch stream with others in a call (client mode)
ascii-chat ${sessionStrings[6]} --url 'https://www.twitch.tv/ludwig'`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-teal-300 mb-3"
          >
            Convert video to ASCII and preview
          </Heading>
          <CodeBlock language="bash">
            {`# Convert video frame to ASCII and preview first 30 lines
ascii-chat mirror --file video.mp4 --snapshot | head -30`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-purple-300 mb-3"
          >
            Play animated GIF
          </Heading>
          <CodeBlock language="bash">
            {`# Loop an animated GIF continuously
ascii-chat mirror --file animation.gif --loop`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-cyan-300 mb-3"
          >
            Convert image to ASCII
          </Heading>
          <CodeBlock language="bash">
            {`# Display JPEG or PNG image as ASCII art
ascii-chat mirror --file photo.jpg --snapshot`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-pink-300 mb-3"
          >
            Pipe video through stdin
          </Heading>
          <CodeBlock language="bash">
            {`# Stream from stdin (useful for chaining commands)
cat video.mp4 | ascii-chat mirror --file -`}
          </CodeBlock>
        </div>

        <div>
          <Heading
            level={3}
            className="text-xl font-semibold text-orange-300 mb-3"
          >
            Render ASCII Art Files and Pipe to FFmpeg
          </Heading>
          <CodeBlock language="bash">
            {`# Render 60 seconds of Twitch stream as ASCII art video
ascii-chat mirror --url 'https://www.twitch.tv/doublelift' --snapshot --snapshot-delay 60 --render-file=ascii-art.mp4

# Pipe ASCII-rendered GIF (400x600 aspect ratio preserved)
ascii-chat mirror --file input.mp4 --render-file="-" --render-file-format=gif | ffmpeg -i pipe:0 -vf "scale=400:600:force_original_aspect_ratio=1,pad=400:600:(ow-iw)/2:(oh-ih)/2:color=black" output.gif

# Preview ASCII video in real-time with ffplay
ascii-chat mirror --file video.mp4 --render-file="-" | ffplay -`}
          </CodeBlock>
        </div>
      </div>
    </section>
  );
}
