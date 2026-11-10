import { Heading } from "@ascii-chat/shared/components";

export default function QuickReferenceSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        📋 Quick Reference
      </Heading>

      <div className="info-box-info mb-5 border-l-4 border-cyan-400">
        <p className="text-gray-300 text-sm mb-2">
          <strong className="text-cyan-300">⌨ Live controls</strong> — these
          shortcuts work while a session is rendering:
        </p>
        <div className="flex flex-wrap gap-2 text-sm">
          <span className="rounded-full bg-yellow-400/10 px-3 py-1 text-yellow-300">
            <code>r</code> render mode
          </span>
          <span className="rounded-full bg-orange-400/10 px-3 py-1 text-orange-300">
            <code>c</code> color mode
          </span>
          <span className="rounded-full bg-pink-400/10 px-3 py-1 text-pink-300">
            <code>f</code> filter
          </span>
          <span className="rounded-full bg-lime-400/10 px-3 py-1 text-lime-300">
            <code>1</code> Matrix
          </span>
          <span className="rounded-full bg-purple-400/10 px-3 py-1 text-purple-300">
            <code>-</code> FPS meter
          </span>
          <span className="rounded-full bg-teal-400/10 px-3 py-1 text-teal-300">
            <code>Space</code> pause
          </span>
          <span className="rounded-full bg-cyan-400/10 px-3 py-1 text-cyan-300">
            <code>← →</code> seek
          </span>
          <span className="rounded-full bg-green-400/10 px-3 py-1 text-green-300">
            <code>?</code> help
          </span>
        </div>
      </div>

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
              <td className="table-body-cell">Monochromatic color tint</td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--matrix</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">Matrix digital rain effect</td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--fps</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">60</td>
              <td className="table-body-cell">Target framerate (1-144)</td>
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
              <td className="table-body-cell">Capture one frame and exit</td>
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
            <tr>
              <td className="table-body-cell">
                <code>--waveform</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">Display audio as a waveform</td>
            </tr>
            <tr className="table-row-alt">
              <td className="table-body-cell">
                <code>--fft</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Display the audio frequency spectrum
              </td>
            </tr>
            <tr>
              <td className="table-body-cell">
                <code>--face-follow</code>
              </td>
              <td className="table-body-cell">—</td>
              <td className="table-body-cell">false</td>
              <td className="table-body-cell">
                Automatically zoom and pan to follow your face
              </td>
            </tr>
          </tbody>
        </table>
      </div>
    </section>
  );
}
