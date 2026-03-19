import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function AudioSetupSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        🎙️ Audio Setup
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Enable Audio (Client)
        </Heading>
        <p className="docs-paragraph">
          Audio is disabled by default in client mode. Enable with{" "}
          <code className="text-cyan-300">--audio</code> or{" "}
          <code className="text-cyan-300">-A</code>:
        </p>
        <CodeBlock language="bash">
          {
            "# Connect with audio enabled\nascii-chat client example.com --audio\n\n# Short form\nascii-chat client example.com -A"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Note:</strong> Audio is only for client mode. Server and
            mirror modes don't have an <code>--audio</code> flag, but support
            audio volume and source controls.
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Audio Source Selection
        </Heading>
        <p className="docs-paragraph">
          Control whether to capture from microphone, media (other
          applications), or both:
        </p>
        <CodeBlock language="bash">
          {
            "# Auto-detection (DEFAULT)\n# Uses microphone unless media is playing\nascii-chat client example.com --audio --audio-source auto\n\n# Microphone only (always capture)\nascii-chat client example.com --audio --audio-source mic\n\n# Media only (for screen sharing audio)\nascii-chat mirror --audio-source media\n\n# Both simultaneously\nascii-chat client example.com --audio --audio-source both"
          }
        </CodeBlock>
        <div className="info-box-info mt-3">
          <p className="text-gray-300 text-sm">
            <strong>Works in:</strong> client mode (with --audio), mirror mode,
            and server mode
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          List Audio Devices
        </Heading>
        <p className="docs-paragraph">
          Discover available microphones and speakers:
        </p>
        <CodeBlock language="bash">
          {
            "# List all microphones (audio input devices)\nascii-chat --list-microphones\n\n# List all speakers (audio output devices)\nascii-chat --list-speakers"
          }
        </CodeBlock>
        <p className="text-gray-400 text-sm mt-2">
          Output shows device index, name, number of channels, default sample
          rate, and system default marker
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Select Microphone and Adjust Volume
        </Heading>
        <CodeBlock language="bash">
          {
            "# Use system default microphone (RECOMMENDED)\nascii-chat client example.com --audio --microphone-index -1\n\n# Use specific microphone (index from --list-microphones)\nascii-chat client example.com --audio --microphone-index 2\n\n# Short form\nascii-chat client example.com --audio -m 2\n\n# Adjust microphone volume (0.0-1.0, default 1.0)\nascii-chat client example.com --audio -m -1 --microphone-volume 0.5\n\n# Alias: --ivolume\nascii-chat client example.com --audio --ivolume 0.8"
          }
        </CodeBlock>
        <div className="info-box-note mt-3">
          <p className="text-gray-300 text-sm">
            Microphone volume is available in client, mirror, and server modes
          </p>
        </div>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Select Speakers and Adjust Volume
        </Heading>
        <CodeBlock language="bash">
          {
            "# Use system default speakers (RECOMMENDED)\nascii-chat client example.com --audio --speakers-index -1\n\n# Use specific speaker device\nascii-chat client example.com --audio --speakers-index 1\n\n# Adjust speaker volume (0.0-1.0, default 1.0)\nascii-chat client example.com --audio --speakers-volume 0.5\n\n# Alias: --volume\nascii-chat client example.com --audio --volume 0.8\n\n# Combine all audio options\nascii-chat client example.com --audio \\\n  --microphone-index -1 --microphone-volume 0.8 \\\n  --speakers-index -1 --speakers-volume 0.7"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Audio Playback Control
        </Heading>
        <CodeBlock language="bash">
          {
            "# Disable speaker output (listen-only mode)\nascii-chat client example.com --audio --no-audio-playback\n\n# Control Opus encoding (default: enabled)\nascii-chat client example.com --audio --encode-audio\nascii-chat client example.com --audio --no-encode-audio"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
