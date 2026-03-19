import { Heading } from "@ascii-chat/shared/components";

export default function UseCasesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-yellow-400">
        💼 Real-World Use Cases
      </Heading>
      <div className="space-y-3">
        <div className="card-standard accent-cyan">
          <Heading level={4} className="text-cyan-300 font-semibold mb-2">
            Movie Night in Terminal
          </Heading>
          <p className="text-gray-300 text-sm">
            Stream movies as ASCII art during development sessions or as a
            novelty. Works over SSH for remote viewing.
          </p>
        </div>
        <div className="card-standard accent-green">
          <Heading level={4} className="text-green-300 font-semibold mb-2">
            Live Presentations
          </Heading>
          <p className="text-gray-300 text-sm">
            Stream live event feeds, screen recordings, or presentation videos
            to all participants simultaneously
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading level={4} className="text-purple-300 font-semibold mb-2">
            Documentation & Tutorials
          </Heading>
          <p className="text-gray-300 text-sm">
            Create ASCII art clips for README files, blog posts, or tutorial
            documentation. Capture specific moments with seek
          </p>
        </div>
        <div className="card-standard accent-teal">
          <Heading level={4} className="text-teal-300 font-semibold mb-2">
            Screen Recording Playback
          </Heading>
          <p className="text-gray-300 text-sm">
            Replay screen recordings as ASCII art. Great for debugging sessions
            or demonstrating terminal-based workflows
          </p>
        </div>
        <div className="card-standard accent-orange">
          <Heading level={4} className="text-orange-300 font-semibold mb-2">
            YouTube Integration
          </Heading>
          <p className="text-gray-300 text-sm">
            Stream YouTube videos directly to terminal. Combine with
            <code className="code-inline">--seek</code> for specific clips
          </p>
        </div>
        <div className="card-standard accent-pink">
          <Heading level={4} className="text-pink-300 font-semibold mb-2">
            Live Streaming Events
          </Heading>
          <p className="text-gray-300 text-sm">
            Connect to live streams (HLS, DASH, RTMP) and display in terminal.
            Works over satellite, cellular, or constrained networks
          </p>
        </div>
        <div className="card-standard accent-yellow">
          <Heading level={4} className="text-yellow-300 font-semibold mb-2">
            Automated Demonstrations
          </Heading>
          <p className="text-gray-300 text-sm">
            Pipe FFmpeg processing chains for automated video analysis or
            real-time monitoring dashboards in the terminal
          </p>
        </div>
      </div>
    </section>
  );
}
