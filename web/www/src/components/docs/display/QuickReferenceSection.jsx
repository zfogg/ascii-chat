import { Heading } from "@ascii-chat/shared/components";

export default function QuickReferenceSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        📋 Quick Reference
      </Heading>

      <div className="table-wrapper">
        <table className="table-base">
          <thead className="table-header">
            <tr>
              <th className="table-header-cell">Option</th>
              <th className="table-header-cell">Short</th>
              <th className="table-header-cell">Default</th>
              <th className="table-header-cell">Description</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td className="table-body-cell">
                <code>--render-mode</code>
              </td>
              <td className="table-body-cell">
                <code>-M</code>
              </td>
              <td className="table-body-cell">foreground</td>
              <td className="table-body-cell">
                Render mode (foreground, background, half-block)
              </td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--palette</code>
              </td>
              <td className="table-body-cell">
                <code>-P</code>
              </td>
              <td className="table-body-cell">standard</td>
              <td className="table-body-cell">ASCII palette preset</td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--palette-chars</code>
              </td>
              <td className="table-body-cell">
                <code>-C</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">
                Custom characters (darkest→brightest)
              </td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--color-filter</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">none</td>
              <td className="table-body-cell">
                Monochromatic color tint
              </td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--matrix</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Matrix digital rain effect
              </td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--fps</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">60</td>
              <td className="table-body-cell">
                Target framerate (1-144)
              </td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--fps-counter</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Show FPS overlay (toggle: <code>-</code>)
              </td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--flip-x</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">Flip horizontally</td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--flip-y</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">Flip vertically</td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--stretch</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Ignore aspect ratio, fill terminal
              </td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--snapshot</code>
              </td>
              <td className="table-body-cell">
                <code>-S</code>
              </td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Capture one frame and exit
              </td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--snapshot-delay</code>
              </td>
              <td className="table-body-cell">
                <code>-D</code>
              </td>
              <td className="table-body-cell">0</td>
              <td className="table-body-cell">
                Delay before snapshot (seconds)
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
  );
}
