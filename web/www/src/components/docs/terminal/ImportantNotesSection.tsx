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
            <strong>🔍 Auto-Detection:</strong> ascii-chat automatically detects
            terminal capabilities at startup using <code>isatty()</code>. When
            output is piped or redirected, detection behaves differently.
          </p>
        </div>

        <div className="info-box-info">
          <p className="text-gray-300 text-sm mb-2">
            <strong>📐 Default Dimensions:</strong> Width 110, Height 70
            characters. Auto-detection via <code>$COLUMNS</code> and{" "}
            <code>$ROWS</code> environment variables (or terminal queries).
            Override with <code>-x</code> and <code>-y</code>.
          </p>
        </div>

        <div className="info-box-note">
          <p className="text-gray-300 text-sm mb-2">
            <strong>🌐 UTF-8 & Unicode:</strong> Automatically detected by
            locale. Use <code>--utf8</code> to override. Some render modes
            (half-block, blocks) require UTF-8 support.
          </p>
        </div>
      </div>
    </section>
  );
}
