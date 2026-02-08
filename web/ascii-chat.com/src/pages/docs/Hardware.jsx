import { CodeBlock } from "@ascii-chat/shared/components";
import { useEffect } from "react";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";

export default function Hardware() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Hardware", path: "/docs/hardware" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Hardware - ascii-chat Documentation"
        description="Learn about webcams, microphones, speakers, and keyboard shortcuts in ascii-chat."
        url="https://ascii-chat.com/docs/hardware"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <h1 className="heading-1 mb-4">
              <span className="text-pink-400">⚙️</span> Hardware Setup
            </h1>
            <p className="text-lg sm:text-xl text-gray-300">
              Webcam, microphone, speaker, and display configuration for
              ascii-chat
            </p>
          </header>

          {/* Important Notes */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-cyan-400">📌 Important Notes</h2>

            <div className="space-y-3">
              <div className="info-box-info">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>🖱️ Keyboard-Driven:</strong> ascii-chat is a
                  keyboard-driven terminal application with{" "}
                  <strong>zero mouse interaction support</strong>. All
                  navigation and controls are done through keyboard input.
                </p>
              </div>

              <div className="info-box-info">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>❓ Help Toggle:</strong> Press{" "}
                  <code className="text-cyan-300">?</code> while running
                  ascii-chat to toggle a TUI help panel showing all available
                  keyboard shortcuts and controls.
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>Device Indices:</strong> Device indices shown by{" "}
                  <code className="text-cyan-300">--list-*</code> commands start
                  at 0. Use <code className="text-cyan-300">-1</code> for
                  microphones and speakers to select the system default device
                  (recommended for most users).
                </p>
              </div>

              <div className="info-box-note">
                <p className="text-gray-300 text-sm mb-2">
                  <strong>Mode-Specific Options:</strong> Some hardware options
                  are only available in certain modes. For example,{" "}
                  <code className="text-cyan-300">--audio</code> is client-only
                  while audio volume controls work in both client and mirror
                  modes.
                </p>
              </div>
            </div>
          </section>

          {/* Webcam Setup */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-purple-400">🎬 Webcam Setup</h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-3">
                List Available Webcams
              </h3>
              <p className="docs-paragraph">
                See all connected webcam devices and their indices (works in
                client and mirror modes):
              </p>
              <CodeBlock language="bash">ascii-chat --list-webcams</CodeBlock>
              <p className="text-gray-400 text-sm mt-2">
                Shows device index, name, and platform-specific details
              </p>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Select Specific Webcam
              </h3>
              <p className="docs-paragraph">
                Use <code className="text-cyan-300">--webcam-index</code> (or
                short form <code className="text-cyan-300">-c</code>) to choose
                a specific camera. (Save it in your{" "}
                <TrackedLink
                  to="/docs/configuration"
                  label="webcam-index config"
                  className="text-cyan-400 hover:text-cyan-300 transition-colors"
                >
                  config file
                </TrackedLink>
                )
              </p>
              <CodeBlock language="bash">
                {
                  "# Use first webcam (default: index 0)\nascii-chat client example.com\n\n# Use second webcam\nascii-chat client example.com --webcam-index 1\n\n# Short form\nascii-chat client example.com -c 2"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Flip Webcam Video
              </h3>
              <p className="docs-paragraph">
                Control horizontal mirroring of webcam output (default: enabled,
                good for front-facing cameras):
              </p>
              <CodeBlock language="bash">
                {
                  "# Enable flip (DEFAULT)\nascii-chat client example.com --webcam-flip\n\n# Disable flip (for rear cameras)\nascii-chat client example.com --no-webcam-flip\n\n# Short form\nascii-chat client example.com -g  # Enable\nascii-chat client example.com -g=false  # Disable"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">
                Test Pattern Mode
              </h3>
              <p className="docs-paragraph">
                Use a test pattern instead of actual webcam (useful for testing,
                CI/CD, or when no camera is available):
              </p>
              <CodeBlock language="bash">
                {
                  "# Test pattern mode\nascii-chat client example.com --test-pattern\nascii-chat mirror --test-pattern\n\n# Via environment variable\nWEBCAM_DISABLED=1 ascii-chat client example.com"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Audio Setup */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-teal-400">🎙️ Audio Setup</h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Enable Audio (Client)
              </h3>
              <p className="docs-paragraph">
                Audio is disabled by default in client mode. Enable with{" "}
                <code className="text-cyan-300">--audio</code> or{" "}
                <code className="text-cyan-300">-A</code>:
              </p>
              <CodeBlock language="bash">
                {
                  "# Connect with audio enabled\nascii-chat client example.com --audio\n\n# Short form\nascii-chat client example.com -A"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Note:</strong> Audio is only for client mode. Server
                  and mirror modes don't have an <code>--audio</code> flag, but
                  support audio volume and source controls.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-3">
                Audio Source Selection
              </h3>
              <p className="docs-paragraph">
                Control whether to capture from microphone, media (other
                applications), or both:
              </p>
              <CodeBlock language="bash">
                {
                  "# Auto-detection (DEFAULT)\n# Uses microphone unless media is playing\nascii-chat client example.com --audio --audio-source auto\n\n# Microphone only (always capture)\nascii-chat client example.com --audio --audio-source mic\n\n# Media only (for screen sharing audio)\nascii-chat mirror --audio-source media\n\n# Both simultaneously\nascii-chat client example.com --audio --audio-source both"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Works in:</strong> client mode (with --audio), mirror
                  mode, and server mode
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">
                List Audio Devices
              </h3>
              <p className="docs-paragraph">
                Discover available microphones and speakers:
              </p>
              <CodeBlock language="bash">
                {
                  "# List all microphones (audio input devices)\nascii-chat --list-microphones\n\n# List all speakers (audio output devices)\nascii-chat --list-speakers"
                }
              </CodeBlock>
              <p className="text-gray-400 text-sm mt-2">
                Output shows device index, name, number of channels, default
                sample rate, and system default marker
              </p>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Select Microphone and Adjust Volume
              </h3>
              <CodeBlock language="bash">
                {
                  "# Use system default microphone (RECOMMENDED)\nascii-chat client example.com --audio --microphone-index -1\n\n# Use specific microphone (index from --list-microphones)\nascii-chat client example.com --audio --microphone-index 2\n\n# Short form\nascii-chat client example.com --audio -m 2\n\n# Adjust microphone volume (0.0-1.0, default 1.0)\nascii-chat client example.com --audio -m -1 --microphone-volume 0.5\n\n# Alias: --ivolume\nascii-chat client example.com --audio --ivolume 0.8"
                }
              </CodeBlock>
              <div className="info-box-note mt-3">
                <p className="text-gray-300 text-sm">
                  Microphone volume is available in client, mirror, and server
                  modes
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">
                Select Speakers and Adjust Volume
              </h3>
              <CodeBlock language="bash">
                {
                  "# Use system default speakers (RECOMMENDED)\nascii-chat client example.com --audio --speakers-index -1\n\n# Use specific speaker device\nascii-chat client example.com --audio --speakers-index 1\n\n# Adjust speaker volume (0.0-1.0, default 1.0)\nascii-chat client example.com --audio --speakers-volume 0.5\n\n# Alias: --volume\nascii-chat client example.com --audio --volume 0.8\n\n# Combine all audio options\nascii-chat client example.com --audio \\\n  --microphone-index -1 --microphone-volume 0.8 \\\n  --speakers-index -1 --speakers-volume 0.7"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-orange-300 mb-3">
                Audio Playback Control
              </h3>
              <CodeBlock language="bash">
                {
                  "# Disable speaker output (listen-only mode)\nascii-chat client example.com --audio --no-audio-playback\n\n# Control Opus encoding (default: enabled)\nascii-chat client example.com --audio --encode-audio\nascii-chat client example.com --audio --no-encode-audio"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Media Streaming */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-indigo-400">🎥 Media Streaming</h2>

            <p className="docs-paragraph mb-6">
              Stream from local files or URLs instead of (or alongside) webcam
              capture. Works in client and mirror modes.
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-indigo-300 mb-3">
                Stream Local Files
              </h3>
              <p className="docs-paragraph">
                Use <code className="text-cyan-300">--file</code> or{" "}
                <code className="text-cyan-300">-f</code> to stream from video,
                audio, or image files (also supports stdin with{" "}
                <code className="text-cyan-300">-</code>). In mirror mode, use{" "}
                <code className="text-cyan-300">--audio</code> to enable audio
                playback from the file:
              </p>
              <CodeBlock language="bash">
                {
                  "# Stream local video file\nascii-chat client example.com -f video.mp4\nascii-chat mirror -f animation.gif\n\n# Mirror mode with audio playback\nascii-chat mirror -f video.mp4 --audio\n\n# Stream from stdin\ncat video.avi | ascii-chat client example.com -f '-'\n\n# Stream from stdin with looping and seeking\ncat video.mov | ascii-chat mirror -f '-' -l -s 00:30"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-blue-300 mb-3">Stream from URLs</h3>
              <p className="docs-paragraph">
                Use <code className="text-cyan-300">--url</code> or{" "}
                <code className="text-cyan-300">-u</code> to stream from web
                URLs including YouTube, RTSP, HTTP, and HTTPS. In mirror mode,
                add <code className="text-cyan-300">--audio</code> to enable
                audio:
              </p>
              <CodeBlock language="bash">
                {
                  "# Stream from YouTube\nascii-chat client example.com --url 'https://youtu.be/7ynHVGCehoM' -s 38:29\n\n# Stream from HTTP/HTTPS\nascii-chat mirror -u 'https://example.com/video.mp4'\n\n# Mirror mode with audio\nascii-chat mirror -u 'https://youtu.be/7ynHVGCehoM' -s 38:29 --audio\n\n# Stream RTSP (IP camera)\nascii-chat client example.com -u 'rtsp://camera.local/stream'"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">Playback Control</h3>
              <CodeBlock language="bash">
                {
                  "# Loop file playback\nascii-chat mirror -f video.mp4 -l\n\n# Start playback paused (toggle with spacebar)\nascii-chat client example.com -f video.mp4 --pause\n\n# Seek to specific timestamp\nascii-chat mirror -f video.mp4 -s 22:10\nascii-chat mirror -f video.mp4 -s 00:30  # MM:SS format\nascii-chat mirror -f video.mp4 -s 1:23:45  # HH:MM:SS format\n\n# Start at specific time and immediately exit (snapshot)\nascii-chat mirror -f video.mp4 -s 5:12 -S -D 0"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-yellow-300 mb-3">
                YouTube & Cookies
              </h3>
              <p className="docs-paragraph">
                Use{" "}
                <code className="text-cyan-300">--cookies-from-browser</code> to
                handle age-restricted or logged-in content:
              </p>
              <CodeBlock language="bash">
                {
                  "# Use cookies from Chrome (default browser)\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' -s 38:29 --cookies-from-browser\n\n# Specify a browser explicitly\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' -s 38:29 --cookies-from-browser firefox\n\n# Supported browsers: chrome, firefox, edge, safari, brave, opera, vivaldi, whale"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Display Options */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-yellow-400">🎨 Display Options</h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-yellow-300 mb-3">Render Modes</h3>
              <p className="docs-paragraph">
                Choose how ASCII characters are rendered (available in client
                and mirror):
              </p>
              <CodeBlock language="bash">
                {
                  "# Foreground mode (DEFAULT - colored text on dark background)\nascii-chat client example.com --render-mode foreground\n\n# Background mode (colored background with white text)\nascii-chat client example.com --render-mode background\n\n# Half-block mode (2x vertical resolution using block characters)\nascii-chat mirror --render-mode half-block -M half-block  # Same thing"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Half-Block Mode:</strong> Provides twice the vertical
                  resolution by using Unicode block characters. Great for
                  detailed images or when you want smaller ASCII art.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-3">ASCII Palettes</h3>
              <p className="docs-paragraph">
                Control which characters are used to render brightness levels:
              </p>
              <CodeBlock language="bash">
                {
                  "# Built-in palettes (all look different)\nascii-chat mirror --palette standard\nascii-chat mirror --palette blocks\nascii-chat mirror --palette digital\nascii-chat mirror --palette minimal\nascii-chat mirror --palette cool\n\n# Custom palette with your own characters\nascii-chat mirror --palette custom --palette-chars '@%#*+=-:. '"
                }
              </CodeBlock>
              <div className="info-box-note mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Custom Palette:</strong> Characters should be ordered
                  from darkest to brightest. More characters = more detail.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Frame Rate and Snapshot
              </h3>
              <CodeBlock language="bash">
                {
                  "# Set target frame rate (1-144 FPS, default 60)\nascii-chat client example.com --fps 30\nascii-chat mirror --fps 10  # Lower for slower machines\n\n# Snapshot mode: capture one frame and exit\nascii-chat client example.com --snapshot\nascii-chat mirror --snapshot\n\n# Snapshot with delay (seconds, default 4.0)\nascii-chat client example.com --snapshot --snapshot-delay 10\n\n# Snapshot immediately (no delay)\nascii-chat mirror -S -D 0\n\n# Pipe to clipboard\nascii-chat mirror -S -D 0 | pbcopy  # macOS\nascii-chat mirror -S -D 0 | xclip    # Linux"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">Aspect Ratio</h3>
              <CodeBlock language="bash">
                {
                  "# Preserve aspect ratio (DEFAULT)\nascii-chat mirror video.mp4\n\n# Allow stretching to fill terminal (may distort image)\nascii-chat mirror --stretch"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Keyboard Shortcuts */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-green-400">⌨️ Keyboard Shortcuts</h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">
                Help & Documentation
              </h3>
              <p className="docs-paragraph">
                Press <code className="text-cyan-300">?</code> to toggle the
                interactive keyboard shortcuts help TUI showing all available
                commands.
              </p>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">
                Common Navigation & Control
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Arrow Keys / HJKL
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Navigate between clients in grid view or focus different
                    streams
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    +/- Keys or =/-
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Adjust speaker volume in real-time during active calls
                  </p>
                </div>
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    Space or M
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Mute/unmute microphone (when audio is enabled)
                  </p>
                </div>
                <div className="card-standard accent-pink">
                  <h4 className="text-pink-300 font-semibold mb-2">
                    Q or Ctrl+C
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Quit ascii-chat gracefully
                  </p>
                </div>
              </div>
            </div>
          </section>

          {/* Option Compatibility */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-orange-400">
              🔗 Option Compatibility
            </h2>

            <p className="docs-paragraph">
              Some options work in specific modes or have dependencies on other
              options.
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-orange-300 mb-3">
                Mode Availability
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-orange">
                  <h4 className="text-orange-300 font-semibold mb-2">
                    --audio flag
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Client & Mirror (with --file/--url):</strong> In
                    client mode, required to enable microphone capture. In
                    mirror mode, required to enable audio playback when using
                    --file or --url.
                  </p>
                </div>

                <div className="card-standard accent-orange">
                  <h4 className="text-orange-300 font-semibold mb-2">
                    Audio Volume/Source Options
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Client, Mirror, Server:</strong>{" "}
                    <code className="text-cyan-300">--microphone-volume</code>,{" "}
                    <code className="text-cyan-300">--speakers-volume</code>,
                    and <code className="text-cyan-300">--audio-source</code>{" "}
                    work in all modes.
                  </p>
                </div>

                <div className="card-standard accent-orange">
                  <h4 className="text-orange-300 font-semibold mb-2">
                    Media Streaming
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Client & Mirror Only:</strong>{" "}
                    <code className="text-cyan-300">--file</code> and{" "}
                    <code className="text-cyan-300">--url</code> not available
                    in server or discovery-service modes.
                  </p>
                </div>

                <div className="card-standard accent-orange">
                  <h4 className="text-orange-300 font-semibold mb-2">
                    Display Options
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Client & Mirror Only:</strong> Render modes,
                    palettes, FPS, and snapshot only work in client and mirror
                    modes.
                  </p>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-yellow-300 mb-3">
                Webcam & Media Options
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-yellow">
                  <h4 className="text-yellow-300 font-semibold mb-2">
                    --test-pattern & --file/--url
                  </h4>
                  <p className="text-gray-300 text-sm mb-2">
                    <strong>Compatible:</strong> Can use together, but
                    <code className="text-cyan-300">--file</code>/
                    <code className="text-cyan-300">--url</code> takes
                    precedence over webcam
                  </p>
                </div>

                <div className="card-standard accent-yellow">
                  <h4 className="text-yellow-300 font-semibold mb-2">
                    --webcam-flip & --webcam-index
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Work Together:</strong> Select device with{" "}
                    <code className="text-cyan-300">--webcam-index</code>, then
                    control flipping with{" "}
                    <code className="text-cyan-300">--webcam-flip</code>
                  </p>
                </div>

                <div className="card-standard accent-yellow">
                  <h4 className="text-yellow-300 font-semibold mb-2">
                    WEBCAM_DISABLED Environment Variable
                  </h4>
                  <p className="text-gray-300 text-sm">
                    <strong>Overrides CLI:</strong> Setting{" "}
                    <code className="text-cyan-300">WEBCAM_DISABLED=1</code>{" "}
                    forces test pattern mode regardless of command-line options.
                    Used by the developer for testing and debugging.
                  </p>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Audio Dependencies
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    --audio flag (Client Only)
                  </h4>
                  <p className="text-gray-300 text-sm mb-2">
                    In <strong>client mode:</strong> Must use{" "}
                    <code className="text-cyan-300">--audio</code> to enable
                    microphone capture. Audio playback works automatically for
                    received audio.
                  </p>
                  <p className="text-gray-300 text-sm">
                    In <strong>mirror mode:</strong> No--audio flag needed.
                    Audio volume controls still apply.
                  </p>
                </div>

                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    Microphone & Speaker Independence
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Can select microphone and speakers independently:
                  </p>
                  <CodeBlock language="bash">
                    {`# USB microphone, built-in speakers
ascii-chat client --audio -m 2 --speakers-index -1

# Built-in mic, USB headset
ascii-chat client --audio -m -1 --speakers-index 1`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    --audio-source Options
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Available in all modes. In client mode, requires{" "}
                    <code className="text-cyan-300">--audio</code> to enable
                    microphone capture, but source selection still applies.
                  </p>
                </div>
              </div>
            </div>
          </section>

          {/* Tips & Tricks */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-blue-400">💡 Tips & Tricks</h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-blue-300 mb-3">Device Discovery</h3>
              <CodeBlock language="bash">
                {
                  "# Always list devices first\nascii-chat --list-webcams\nascii-chat --list-microphones\nascii-chat --list-speakers\n\n# Note the indices for your devices\n# Then use them in your commands"
                }
              </CodeBlock>
              <div className="info-box-info mt-3">
                <p className="text-gray-300 text-sm">
                  <strong>Tip:</strong> Use{" "}
                  <code className="text-cyan-300">-1</code> for microphone and
                  speaker indices to always use the system default. This
                  automatically adapts when you plug/unplug devices.
                </p>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">Common Setups</h3>
              <div className="space-y-3">
                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Basic Video Call
                  </h4>
                  <CodeBlock language="bash">
                    ascii-chat client example.com
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    With Audio
                  </h4>
                  <CodeBlock language="bash">
                    ascii-chat client example.com --audio
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Custom Audio Devices
                  </h4>
                  <CodeBlock language="bash">
                    {`ascii-chat client example.com --audio \\
  --microphone-index 2 --microphone-volume 0.8 \\
  --speakers-index 1 --speakers-volume 0.7`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    High-Quality Display
                  </h4>
                  <CodeBlock language="bash">
                    {`ascii-chat client example.com \\
  --render-mode half-block --palette blocks --fps 60`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Stream YouTube Video
                  </h4>
                  <CodeBlock language="bash">
                    {`ascii-chat client example.com \\
  --url 'https://youtu.be/7ynHVGCehoM' -s 38:29 --audio`}
                  </CodeBlock>
                </div>

                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Low Bandwidth
                  </h4>
                  <CodeBlock language="bash">
                    {`ascii-chat client example.com --audio \\
  --fps 30 --palette minimal \\
  --microphone-volume 0.5 --speakers-volume 0.5`}
                  </CodeBlock>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-3">
                Troubleshooting
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    Webcam Not Found
                  </h4>
                  <ol className="list-decimal list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      Run <code className="text-cyan-300">--list-webcams</code>{" "}
                      to see all devices
                    </li>
                    <li>Check that device index matches what you're using</li>
                    <li>
                      Try <code className="text-cyan-300">--test-pattern</code>{" "}
                      to verify ascii-chat works
                    </li>
                    <li>
                      Check OS-level permissions (camera access in System
                      Settings)
                    </li>
                  </ol>
                </div>

                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    No Audio
                  </h4>
                  <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      In client mode, add{" "}
                      <code className="text-cyan-300">--audio</code>
                    </li>
                    <li>
                      Run{" "}
                      <code className="text-cyan-300">--list-microphones</code>{" "}
                      and <code className="text-cyan-300">--list-speakers</code>{" "}
                      to verify devices exist
                    </li>
                    <li>Try using system default: -m -1 --speakers-index -1</li>
                    <li>Check OS-level audio permissions</li>
                  </ul>
                </div>

                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    Video is Flipped
                  </h4>
                  <p className="text-gray-300 text-sm">
                    Flip is enabled by default (good for front cameras). Disable
                    for rear/fixed cameras:
                    <br />
                    <code className="text-cyan-300">--no-webcam-flip</code>
                  </p>
                </div>

                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    Media File Not Playing
                  </h4>
                  <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                    <li>
                      Ensure ffmpeg is installed (used for format support)
                    </li>
                    <li>Check file path is correct and readable</li>
                    <li>
                      Try{" "}
                      <code className="text-cyan-300">
                        cat file.mp4 | ascii-chat mirror -f '-'
                      </code>{" "}
                      to test stdin piping
                    </li>
                  </ul>
                </div>

                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    YouTube Won't Stream
                  </h4>
                  <ul className="list-disc list-inside text-gray-300 text-sm space-y-1">
                    <li>Install yt-dlp: required for YouTube support</li>
                    <li>
                      For age-restricted videos, use{" "}
                      <code className="text-cyan-300">
                        --cookies-from-browser
                      </code>
                    </li>
                    <li>For login-required content, same cookie solution</li>
                  </ul>
                </div>
              </div>
            </div>
          </section>

          <Footer />
        </div>
      </div>
    </>
  );
}
