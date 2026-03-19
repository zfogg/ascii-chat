import { Heading } from "@ascii-chat/shared/components";

export default function PerformanceTipsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        💡 Performance Tips
      </Heading>
      <div className="space-y-3">
        <div className="card-standard accent-cyan">
          <Heading level={4} className="text-cyan-300 font-semibold mb-2">
            Optimize for Network
          </Heading>
          <p className="text-gray-400 text-sm">
            Reduce <code className="code-inline">--fps</code> and use
            lower
            <code className="code-inline">--color-mode</code> on slow
            connections. Mono rendering is fastest.
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading
            level={4}
            className="text-purple-300 font-semibold mb-2"
          >
            Bandwidth Usage
          </Heading>
          <p className="text-gray-400 text-sm">
            Typical usage: monochrome at 10 fps ≈ 10-20 Kbps, truecolor at
            30 fps ≈ 200-500 Kbps. Add audio for +50-100 Kbps.
          </p>
        </div>
        <div className="card-standard accent-green">
          <Heading
            level={4}
            className="text-green-300 font-semibold mb-2"
          >
            CPU Usage
          </Heading>
          <p className="text-gray-400 text-sm">
            SIMD-accelerated ASCII conversion (1-4x speedup). Smaller
            output dimensions = lower CPU. Run on weak hardware with
            <code className="code-inline">--width 80 --height 24</code>.
          </p>
        </div>
      </div>
    </section>
  );
}
