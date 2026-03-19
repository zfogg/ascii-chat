import { Heading } from "@ascii-chat/shared/components";

export default function AdvancedOptionsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        ⚙️ Advanced Options
      </Heading>

      <div className="space-y-3">
        <div className="card-standard accent-cyan">
          <Heading level={4} className="text-cyan-300 font-semibold mb-2">
            Frame Rate Control
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--fps N</code> Set target frame rate
            (default: 30)
          </p>
          <p className="text-gray-400 text-xs">
            Example: <code className="code-inline">--fps 60</code> for smooth
            playback, <code className="code-inline">--fps 10</code> for
            bandwidth-constrained networks
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading level={4} className="text-purple-300 font-semibold mb-2">
            Audio Passthrough
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--audio</code> Enable audio from media
            file
          </p>
          <p className="text-gray-400 text-xs">
            Audio uses Opus codec for efficient low-bandwidth streaming
          </p>
        </div>
        <div className="card-standard accent-green">
          <Heading level={4} className="text-green-300 font-semibold mb-2">
            Loop Playback
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--loop</code> Repeat media playback
            continuously
          </p>
          <p className="text-gray-400 text-xs">
            Useful for presentations or background displays
          </p>
        </div>
        <div className="card-standard accent-teal">
          <Heading level={4} className="text-teal-300 font-semibold mb-2">
            Scaling
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--width, --height</code> Control
            display dimensions
          </p>
          <p className="text-gray-400 text-xs">
            Example:{" "}
            <code className="code-inline">--width 200 --height 50</code>
            for widescreen displays
          </p>
        </div>
        <div className="card-standard accent-orange">
          <Heading level={4} className="text-orange-300 font-semibold mb-2">
            Palettes
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--palette blocks</code> Choose
            character set for rendering
          </p>
          <p className="text-gray-400 text-xs">
            Options: <code className="code-inline">detailed</code>,
            <code className="code-inline">blocks</code>,
            <code className="code-inline">minimal</code>
          </p>
        </div>
        <div className="card-standard accent-pink">
          <Heading level={4} className="text-pink-300 font-semibold mb-2">
            Seeking
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            <code className="code-inline">--seek SECONDS</code> Jump to specific
            timestamp
          </p>
          <p className="text-gray-400 text-xs">
            Works with local files and HTTP progressive downloads
          </p>
        </div>
      </div>
    </section>
  );
}
