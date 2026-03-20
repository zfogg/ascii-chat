import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function SnapshotDelaySection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-pink-400">
        ⏱️ Snapshot Delay & Timing
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Zero-Delay Capture (-D 0)
        </Heading>
        <p className="docs-paragraph">
          Capture the very first frame with no warmup delay (useful for
          scripting and integration tests):
        </p>
        <CodeBlock language="bash">
          {
            "# Capture immediately\nascii-chat mirror -S -D 0\n\n# To clipboard immediately\nascii-chat mirror -S -D 0 | pbcopy\n\n# Save to file immediately\nascii-chat mirror -S -D 0 > frame.txt"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Warmup Period
        </Heading>
        <p className="docs-paragraph">
          By default, <code className="text-cyan-300">--snapshot</code> waits 4
          seconds to allow webcam adjustment. For faster captures:
        </p>
        <CodeBlock language="bash">
          {
            "# 1 second (quick check)\nascii-chat mirror --snapshot --snapshot-delay 1\n\n# 2 seconds (compromise)\nascii-chat mirror --snapshot --snapshot-delay 2"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <div className="info-box-warning">
          <p className="text-gray-300 text-sm mb-3">
            <strong>⚠️ macOS Webcam Warmup Gotcha</strong>
          </p>
          <p className="text-gray-400 text-sm mb-2">
            All macOS laptops have a hardware warmup period where the camera
            shows black frames for 1-3 seconds after being accessed. This is
            OS-level behavior and <strong>cannot be bypassed</strong>.
          </p>
          <p className="text-gray-400 text-sm mb-2">
            <strong>Symptoms:</strong> Using{" "}
            <code className="text-cyan-300">-D 0</code> or very short delays
            result in solid black frames instead of your webcam feed.
          </p>
          <p className="text-gray-400 text-sm">
            <strong>Solution:</strong> Use at least 3-4 seconds of warmup time.
            The default 4 seconds is optimal for most macOS cameras. If you
            consistently get black frames, increase to{" "}
            <code className="text-cyan-300">-D 5</code>.
          </p>
        </div>
      </div>
    </section>
  );
}
