import { Heading } from "@ascii-chat/shared/components";

export default function ImportantNotesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        📌 Important Notes
      </Heading>

      <div className="space-y-3">
        <div className="info-box-info">
          <p className="text-gray-300 text-sm mb-2">
            <strong>🖱️ Keyboard-Driven:</strong> ascii-chat is a keyboard-driven
            terminal application with{" "}
            <strong>zero mouse interaction support</strong>. All navigation and
            controls are done through keyboard input.
          </p>
        </div>

        <div className="info-box-info">
          <p className="text-gray-300 text-sm mb-2">
            <strong>❓ Help Toggle:</strong> Press{" "}
            <code className="text-cyan-300">?</code> while running ascii-chat to
            toggle a TUI help panel showing all available keyboard shortcuts and
            controls.
          </p>
        </div>

        <div className="info-box-note">
          <p className="text-gray-300 text-sm mb-2">
            <strong>Device Indices:</strong> Device indices shown by{" "}
            <code className="text-cyan-300">--list-*</code> commands start at 0.
            Use <code className="text-cyan-300">-1</code> for microphones and
            speakers to select the system default device (recommended for most
            users).
          </p>
        </div>
      </div>
    </section>
  );
}
