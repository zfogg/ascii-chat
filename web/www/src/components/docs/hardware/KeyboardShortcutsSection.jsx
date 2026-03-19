import { Heading } from "@ascii-chat/shared/components";

export default function KeyboardShortcutsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        ⌨️ Keyboard Shortcuts
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Help & Documentation
        </Heading>
        <p className="docs-paragraph">
          Press <code className="text-cyan-300">?</code> to toggle the
          interactive keyboard shortcuts help TUI showing all available
          commands.
        </p>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Keyboard Shortcuts
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-cyan">
            <Heading
              level={4}
              className="text-cyan-300 font-semibold mb-2"
            >
              Arrow Keys
            </Heading>
            <p className="text-gray-300 text-sm">
              Navigate between clients in grid view or focus different
              streams
            </p>
          </div>
          <div className="card-standard accent-yellow">
            <Heading
              level={4}
              className="text-yellow-300 font-semibold mb-2"
            >
              R
            </Heading>
            <p className="text-gray-300 text-sm">
              Cycle through render modes (foreground, background,
              half-block)
            </p>
          </div>
          <div className="card-standard accent-orange">
            <Heading
              level={4}
              className="text-orange-300 font-semibold mb-2"
            >
              C
            </Heading>
            <p className="text-gray-300 text-sm">
              Cycle through color modes (auto, none, 16, 256, truecolor)
            </p>
          </div>
          <div className="card-standard accent-pink">
            <Heading
              level={4}
              className="text-pink-300 font-semibold mb-2"
            >
              F
            </Heading>
            <p className="text-gray-300 text-sm">
              Cycle through color filters
            </p>
          </div>
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-2"
            >
              Space
            </Heading>
            <p className="text-gray-300 text-sm">
              Pause/unpause media playback (when file or URL media is
              playing)
            </p>
          </div>
          <div className="card-standard accent-lime">
            <Heading
              level={4}
              className="text-lime-300 font-semibold mb-2"
            >
              0 (Zero)
            </Heading>
            <p className="text-gray-300 text-sm">
              Toggle Matrix-style digital rain effect
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-2"
              id="fps-meter"
            >
              -
            </Heading>
            <p className="text-gray-300 text-sm">
              Toggle the FPS meter display
            </p>
          </div>
          <div className="card-standard accent-red">
            <Heading
              level={4}
              className="text-red-300 font-semibold mb-2"
            >
              Esc or Ctrl+C
            </Heading>
            <p className="text-gray-300 text-sm">
              Exit the current TUI screen or quit ascii-chat
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}
