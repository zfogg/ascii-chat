import { Heading } from "@ascii-chat/shared/components";

export default function TipsGotchasSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        💡 Tips, Tricks & Common Gotchas
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Common Gotchas & Solutions
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-pink">
            <Heading level={4} className="text-pink-300 font-semibold mb-2">
              Black Frames on macOS
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              <strong>Problem:</strong> Using{" "}
              <code className="text-cyan-300">-D 0</code> gives a completely
              black frame
            </p>
            <p className="text-gray-400 text-sm">
              <strong>Solution:</strong> Use at least{" "}
              <code className="text-cyan-300">-D 3</code> or higher. macOS
              hardware needs time to warm up.
            </p>
          </div>
          <div className="card-standard accent-cyan">
            <Heading level={4} className="text-cyan-300 font-semibold mb-2">
              Empty or Corrupted Output
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              <strong>Problem:</strong> Output file is empty or contains garbage
            </p>
            <p className="text-gray-400 text-sm">
              <strong>Solution:</strong> Check exit code:{" "}
              <code className="text-cyan-300">echo $?</code>. Non-zero means the
              command failed. Check for error messages in stderr.
            </p>
          </div>
          <div className="card-standard accent-teal">
            <Heading level={4} className="text-teal-300 font-semibold mb-2">
              ANSI Codes in Piped Output
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              <strong>Problem:</strong> Output contains ANSI escape sequences
              even when piped
            </p>
            <p className="text-gray-400 text-sm">
              <strong>Solution:</strong> Use{" "}
              <code className="text-cyan-300">--color-mode mono</code> or{" "}
              <code className="text-cyan-300">--color false</code> to disable
              colors completely.
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Network Timeouts
            </Heading>
            <p className="text-gray-400 text-sm mb-2">
              <strong>Problem:</strong> Client snapshot hangs or times out
              waiting for server
            </p>
            <p className="text-gray-400 text-sm">
              <strong>Solution:</strong> Use the system{" "}
              <code className="text-cyan-300">timeout</code> command:{" "}
              <code className="text-cyan-300">
                timeout 10 ascii-chat client ...
              </code>
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}
