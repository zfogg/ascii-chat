import { Heading } from "@ascii-chat/shared/components";
import TrackedLink from "../../TrackedLink";

export default function ImportantNotesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-fuchsia-400">
        📌 Important Notes
      </Heading>

      <div className="space-y-3">
        <div className="info-box-info">
          <p className="text-gray-300 text-sm mb-2">
            <strong>🎮 Interactive Controls:</strong> Many display options
            can be toggled at runtime. Press <code>r</code> to cycle
            render modes, <code>c</code> to cycle color modes, and{" "}
            <code>-</code> to toggle the FPS counter.
          </p>
        </div>

        <div className="info-box-note">
          <p className="text-gray-300 text-sm mb-2">
            <strong>🎨 Color Filters:</strong> Using{" "}
            <code>--color-filter</code> automatically sets{" "}
            <code>--color-mode</code> to mono. The filter tints grayscale
            video with a single color.
          </p>
        </div>

        <div className="info-box-note">
          <p className="text-gray-300 text-sm mb-2">
            <strong>🖥️ Terminal Settings:</strong> For color mode, UTF-8,
            dimensions, and terminal capabilities, see the{" "}
            <TrackedLink
              to="/docs/terminal"
              label="terminal settings"
              className="text-cyan-400 hover:text-cyan-300 transition-colors"
            >
              Terminal
            </TrackedLink>{" "}
            page.
          </p>
        </div>
      </div>
    </section>
  );
}
