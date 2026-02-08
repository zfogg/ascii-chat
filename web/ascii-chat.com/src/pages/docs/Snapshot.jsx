import { useEffect } from "react";
import Footer from "../../components/Footer";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";
import { AsciiChatHead } from "../../components/AsciiChatHead";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function Snapshot() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Snapshot Mode", path: "/docs/snapshot" },
    ]);
  }, []);
  return (
    <>
      <AsciiChatHead
        title="Snapshot Mode - ascii-chat Documentation"
        description="Single-frame capture, scripting, and automation examples with ascii-chat snapshot mode."
        url="https://ascii-chat.com/docs/snapshot"
      />
      <div className="bg-gray-950 text-gray-100 flex flex-col">
        <div className="flex-1 flex flex-col docs-container">
          <header className="mb-12 sm:mb-16">
            <h1 className="heading-1 mb-4">
              <span className="text-teal-400">📸</span> Snapshot Mode
            </h1>
            <p className="text-lg sm:text-xl text-gray-300">
              Single-frame capture, scripting, automation, and debugging
            </p>
          </header>

          {/* Basic Usage */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-cyan-400">⚡ Quick Start</h2>
            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Single Frame Capture
              </h3>
              <p className="docs-paragraph">
                Capture one frame from webcam and save as ASCII art:
              </p>
              <CodeBlock language="bash">
                {
                  "# Capture and display\nascii-chat mirror --snapshot\n\n# Capture and save to file\nascii-chat mirror --snapshot > output.txt"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">Timed Capture</h3>
              <p className="docs-paragraph">
                Capture for a specific duration, then exit:
              </p>
              <CodeBlock language="bash">
                {
                  "# Capture for 5 seconds\nascii-chat mirror --snapshot --snapshot-delay 5\n\n# Capture for specific duration then exit\nascii-chat client example.com --snapshot --snapshot-delay 3"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Understanding Output & Piping */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-purple-400">
              🔌 Piping, Redirection & isatty()
            </h2>

            <p className="docs-paragraph">
              ascii-chat automatically detects whether stdout is connected to a
              terminal (TTY) or being piped/redirected. This enables clean
              output for scripting:
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-purple-300 mb-3">
                How Output Detection Works
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Terminal (TTY)
                  </h4>
                  <p className="text-gray-400 text-sm">
                    When stdout is a terminal, ascii-chat outputs with ANSI
                    codes, cursor movement, and terminal optimizations for live
                    display
                  </p>
                </div>
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    Piped/Redirected (Non-TTY)
                  </h4>
                  <p className="text-gray-400 text-sm">
                    When stdout is piped or redirected to a file, ascii-chat
                    automatically outputs clean ASCII without ANSI codes, cursor
                    movement, or RLE compression
                  </p>
                </div>
              </div>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Practical Examples
              </h3>
              <CodeBlock language="bash">
                {
                  "# Terminal: displayed with colors and animations\nascii-chat mirror --snapshot\n\n# File: clean ASCII saved to disk\nascii-chat mirror --snapshot > frame.txt\n\n# Pipe: clean ASCII sent downstream\nascii-chat mirror --snapshot | pbcopy\n\n# Viewing piped output\nascii-chat mirror --snapshot | cat -\n\n# Force plain ASCII (no color codes at all)\nascii-chat mirror --snapshot --color-mode mono > plain.txt"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Clipboard Magic (macOS)
              </h3>
              <CodeBlock language="bash">
                {
                  "# Capture to clipboard\nascii-chat mirror --snapshot | pbcopy\n\n# Display clipboard contents\npbpaste | cat -\n\n# Capture AND display\nascii-chat mirror --snapshot | tee /tmp/frame.txt | pbcopy\n\n# Capture from clipboard, modify, save back\npbpaste > frame.txt\nnvim frame.txt  # Edit in neovim\ncat frame.txt | pbcopy  # Copy modified version back"
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">Color Snapshots</h3>
              <p className="docs-paragraph">
                By default, colors are only enabled when stdout is connected to
                a terminal. When piping or redirecting (when{" "}
                <code className="text-cyan-300">isatty()</code> returns false),
                colors are automatically disabled to keep output clean. To
                capture colored snapshots when piping or redirecting, explicitly
                enable colors with{" "}
                <code className="text-cyan-300">--color</code> or{" "}
                <code className="text-cyan-300">--color=true</code>:
              </p>
              <CodeBlock language="bash">
                {
                  "# Terminal: colors enabled by default\nascii-chat mirror --snapshot\n\n# Piped/redirected: colors disabled by default (clean ASCII)\nascii-chat mirror --snapshot > frame.txt\nascii-chat mirror --snapshot | pbcopy\n\n# Piped/redirected: enable colors explicitly\nascii-chat mirror --snapshot --color > frame_color.txt\nascii-chat mirror --snapshot --color | pbcopy\n\n# Specify color mode (--color-mode auto is the default)\nascii-chat mirror --snapshot --color --color-mode 256 > frame_256.txt\nascii-chat mirror --snapshot --color --color-mode truecolor > frame_true.txt\n\n# Force plain ASCII (no color codes)\nascii-chat mirror --snapshot --color-mode mono > plain.txt"
                }
              </CodeBlock>
            </div>
          </section>

          {/* Snapshot Delay & Timing */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-pink-400">
              ⏱️ Snapshot Delay & Timing
            </h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Zero-Delay Capture (-D 0)
              </h3>
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
              <h3 className="heading-3 text-green-300 mb-3">Warmup Period</h3>
              <p className="docs-paragraph">
                By default, <code className="text-cyan-300">--snapshot</code>{" "}
                waits 4 seconds to allow webcam adjustment. For faster captures:
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
                  All macOS laptops have a hardware warmup period where the
                  camera shows black frames for 1-3 seconds after being
                  accessed. This is OS-level behavior and{" "}
                  <strong>cannot be bypassed</strong>.
                </p>
                <p className="text-gray-400 text-sm mb-2">
                  <strong>Symptoms:</strong> Using{" "}
                  <code className="text-cyan-300">-D 0</code> or very short
                  delays result in solid black frames instead of your webcam
                  feed.
                </p>
                <p className="text-gray-400 text-sm">
                  <strong>Solution:</strong> Use at least 3-4 seconds of warmup
                  time. The default 4 seconds is optimal for most macOS cameras.
                  If you consistently get black frames, increase to{" "}
                  <code className="text-cyan-300">-D 5</code>.
                </p>
              </div>
            </div>
          </section>

          {/* HTTP/HTTPS, RTMP & YouTube Snapshots */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-pink-400">
              🎥 HTTP/HTTPS, RTMP & YouTube Snapshots
            </h2>

            <p className="docs-paragraph">
              Capture ASCII art frames from any media source that ffmpeg
              supports—including YouTube videos, HTTP(S) streaming, RTMP
              streams, and remote video files. Use{" "}
              <code className="text-cyan-300">--url</code> to specify the source
              and <code className="text-cyan-300">--seek</code> to jump to a
              specific timestamp (seek works with files; live streams snapshot
              the current frame):
            </p>

            <CodeBlock language="bash">
              {
                '#!/bin/bash\n# Capture ASCII art from YouTube, HTTP(S), and RTMP streams\n\n# === YOUTUBE ===\n# Capture opening frame of a YouTube video\nascii-chat mirror --url "https://youtu.be/7ynHVGCehoM" --snapshot \\\n  --color-mode truecolor > youtube_frame.txt\n\n# Jump to 30 seconds and capture\nascii-chat mirror --url "https://youtu.be/7ynHVGCehoM" \\\n  --seek 30 --snapshot --color-mode truecolor > youtube_30sec.txt\n\n# Jump to 1 minute 45 seconds\nascii-chat mirror --url "https://youtu.be/7ynHVGCehoM" \\\n  --seek 105 --snapshot > youtube_105sec.txt\n\n# Process multiple timestamps from one video\nfor time in 0 30 60 90 120; do\n  ascii-chat mirror --url "https://youtu.be/7ynHVGCehoM" \\\n    --seek $time --snapshot --color-mode mono > "frame_${time}s.txt"\ndone\n\n# === HTTP(S) MEDIA ===\n# Capture from HTTP(S) video file\nascii-chat mirror --url "https://example.com/videos/demo.mp4" \\\n  --seek 60 --snapshot --color > http_video_60s.txt\n\n# === RTMP LIVE STREAMS ===\n# Snapshot current frame from RTMP live stream\nascii-chat mirror --url "rtmp://live.example.com/stream" --snapshot > rtmp_live.txt\n\n# Capture RTMP stream with color\nascii-chat mirror --url "rtmp://live.example.com/app/stream" \\\n  --snapshot --color-mode truecolor > rtmp_colored.txt\n\n# Quick preview to clipboard\nascii-chat mirror --url "https://youtu.be/7ynHVGCehoM" --seek 60 \\\n  --snapshot --color | pbcopy'
              }
            </CodeBlock>
          </section>

          {/* Debugging & Verification */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-green-400">
              🔍 Debugging & Network Verification
            </h2>

            <p className="docs-paragraph">
              Snapshot mode is perfect for verifying your setup. A successful
              snapshot means the entire pipeline is working:
            </p>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">
                What a Successful Snapshot Verifies
              </h3>
              <ul className="space-y-1 text-gray-300 text-sm">
                <li>
                  <span className="text-cyan-300">✅ Webcam Access</span> —
                  Webcam is working and accessible to ascii-chat
                </li>
                <li>
                  <span className="text-teal-300">✅ Video Encoding</span> —
                  Image capture, scaling, and ASCII conversion all work
                  correctly
                </li>
                <li>
                  <span className="text-purple-300">✅ Terminal Rendering</span>{" "}
                  — Terminal capabilities detected and ANSI codes generated
                  correctly
                </li>
                <li>
                  <span className="text-pink-300">
                    ✅ Network (Client Mode)
                  </span>{" "}
                  — Connection to server succeeded and one frame was received
                </li>
                <li>
                  <span className="text-green-300">
                    ✅ Crypto Handshake (Client Mode)
                  </span>{" "}
                  — Encryption negotiation succeeded, keys were established
                </li>
                <li>
                  <span className="text-cyan-300">
                    ✅ ACDS (Discovery Mode)
                  </span>{" "}
                  — Discovery service lookup, NAT traversal, and peer connection
                  all worked
                </li>
              </ul>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Verification Examples
              </h3>
              <CodeBlock language="bash">
                {
                  '# Test local webcam only\nascii-chat mirror -S -D 0\necho "Exit code: $?"  # 0 = success\n\n# Test network connection to server\nascii-chat client example.com -S -D 3\necho "Exit code: $?"  # Verifies: network, crypto, frame transmission\n\n# Test with specific encryption\nascii-chat client example.com --key ~/.ssh/id_ed25519 -S -D 3\necho "Exit code: $?"  # Verifies: key auth, handshake, transmission\n\n# Test ACDS discovery and WebRTC\nascii-chat my-session-string -S -D 5\necho "Exit code: $?"  # Verifies: ACDS lookup, NAT traversal, P2P connection'
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">
                Exit Codes for Scripting
              </h3>
              <CodeBlock language="bash">
                {
                  '# Check if snapshot succeeded\nif ascii-chat mirror -S -D 1 > /dev/null 2>&1; then\n  echo "✓ Webcam working"\nelse\n  echo "✗ Webcam failed (exit code: $?)"\nfi\n\n# Verify server connection\nif ascii-chat client example.com:8080 -S -D 3 > /tmp/frame.txt 2>&1; then\n  echo "✓ Connected to server and received frame"\n  cat /tmp/frame.txt | head -20  # Show first 20 lines\nelse\n  echo "✗ Server connection failed (exit code: $?)"\nfi\n\n# Retry logic\nfor attempt in {1..3}; do\n  echo "Attempt $attempt..."\n  if ascii-chat client example.com -S -D 2; then\n    echo "Success!"\n    break\n  fi\n  echo "Failed, retrying..."\n  sleep 2\ndone'
                }
              </CodeBlock>
            </div>
          </section>

          {/* Scripting Tips & Tricks */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-purple-400">
              💡 Tips, Tricks & Common Gotchas
            </h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-pink-300 mb-3">
                Common Gotchas & Solutions
              </h3>
              <div className="space-y-3">
                <div className="card-standard accent-pink">
                  <h4 className="text-pink-300 font-semibold mb-2">
                    Black Frames on macOS
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    <strong>Problem:</strong> Using{" "}
                    <code className="text-cyan-300">-D 0</code> gives a
                    completely black frame
                  </p>
                  <p className="text-gray-400 text-sm">
                    <strong>Solution:</strong> Use at least{" "}
                    <code className="text-cyan-300">-D 3</code> or higher. macOS
                    hardware needs time to warm up.
                  </p>
                </div>
                <div className="card-standard accent-cyan">
                  <h4 className="text-cyan-300 font-semibold mb-2">
                    Empty or Corrupted Output
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    <strong>Problem:</strong> Output file is empty or contains
                    garbage
                  </p>
                  <p className="text-gray-400 text-sm">
                    <strong>Solution:</strong> Check exit code:{" "}
                    <code className="text-cyan-300">echo $?</code>. Non-zero
                    means the command failed. Check for error messages in
                    stderr.
                  </p>
                </div>
                <div className="card-standard accent-teal">
                  <h4 className="text-teal-300 font-semibold mb-2">
                    ANSI Codes in Piped Output
                  </h4>
                  <p className="text-gray-400 text-sm mb-2">
                    <strong>Problem:</strong> Output contains ANSI escape
                    sequences even when piped
                  </p>
                  <p className="text-gray-400 text-sm">
                    <strong>Solution:</strong> Use{" "}
                    <code className="text-cyan-300">--color-mode mono</code> or{" "}
                    <code className="text-cyan-300">--color false</code> to
                    disable colors completely.
                  </p>
                </div>
                <div className="card-standard accent-purple">
                  <h4 className="text-purple-300 font-semibold mb-2">
                    Network Timeouts
                  </h4>
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

          {/* Advanced Scripting */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-green-400">
              🚀 Advanced Scripting Examples
            </h2>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-green-300 mb-3">
                Batch Processing Images
              </h3>
              <CodeBlock language="bash">
                {
                  '# Convert all images in directory\nfor img in *.jpg; do\n  echo "Converting $img..."\n  ascii-chat mirror --file "$img" --snapshot > "ascii_${img}.txt"\ndone\n\n# Convert with specific palette and dimensions\nfor img in *.png; do\n  ascii-chat mirror --file "$img" --snapshot \\\n    --palette blocks --width 100 --height 50 \\\n    > "ascii_${img}.txt"\ndone'
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-cyan-300 mb-3">
                Integration Testing
              </h3>
              <CodeBlock language="bash">
                {
                  '#!/bin/bash\n# Integration test: verify entire stack works\n\necho "Testing webcam..."\nif ! ascii-chat mirror -S -D 2 > /tmp/local.txt 2>&1; then\n  echo "✗ Webcam test failed"\n  exit 1\nfi\necho "✓ Webcam working"\n\necho "Testing server connection..."\nif ! ascii-chat client example.com -S -D 3 > /tmp/remote.txt 2>&1; then\n  echo "✗ Network test failed"\n  exit 1\nfi\necho "✓ Network connection working"\n\necho "Testing encrypted connection..."\nif ! ascii-chat client example.com --key ~/.ssh/id_ed25519 -S -D 3 > /tmp/encrypted.txt 2>&1; then\n  echo "✗ Encryption test failed"\n  exit 1\nfi\necho "✓ Encryption working"\n\necho "All tests passed!"\nexit 0'
                }
              </CodeBlock>
            </div>

            <div className="docs-subsection-spacing">
              <h3 className="heading-3 text-teal-300 mb-3">
                Video Frame Extraction
              </h3>
              <CodeBlock language="bash">
                {
                  '#!/bin/bash\n# Extract frames from video at intervals\n\nVIDEO="$1"\nFRAMES_PER_SECOND=1\n\nif [ ! -f "$VIDEO" ]; then\n  echo "Usage: $0 <video-file>"\n  exit 1\nfi\n\nmkdir -p frames\n\n# Get video duration\nDURATION=$(ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1:nk=1 "$VIDEO")\nFRAMES=$(echo "$DURATION * $FRAMES_PER_SECOND" | bc)\n\necho "Extracting ~$FRAMES frames from $VIDEO"\n\nfor i in $(seq 0 $((1/$FRAMES_PER_SECOND)) $(echo "$DURATION - 1" | bc)); do\n  TIME=$(printf "%02d:%02d:%06.3f" $((${i%.*}/60/60)) $(((${i%.*}/60)%60)) $(bc -l <<< "$i - int($i) + int($i)"))\n  ascii-chat mirror --file "$VIDEO" --seek "$i" -S -D 1 \\\n    --color-mode mono --palette minimal > "frames/frame_$(printf "%06d" $i).txt"\ndone\n\necho "Frames saved to frames/ directory"'
                }
              </CodeBlock>
            </div>
          </section>

          {/* Use Cases */}
          <section className="docs-section-spacing">
            <h2 className="heading-2 text-teal-400">💼 Real-World Use Cases</h2>
            <div className="space-y-3">
              <div className="card-standard accent-yellow">
                <h4 className="text-yellow-300 font-semibold mb-2">
                  YouTube, RTMP & Streaming Media
                </h4>
                <p className="text-gray-300 text-sm mb-2">
                  Capture ASCII art frames from any media source—YouTube videos,
                  HTTP(S) files, RTMP live streams, and more. Use{" "}
                  <code className="text-cyan-300">--url</code> and{" "}
                  <code className="text-cyan-300">--seek</code> to grab frames
                  at specific timestamps for documentation, analysis, or
                  creative projects.
                </p>
                <p className="text-gray-400 text-xs">
                  <strong>Tip:</strong> Use{" "}
                  <code className="text-cyan-300">yt-dlp</code> to download
                  videos first, then snapshot locally. See{" "}
                  <code className="text-cyan-300">man yt-dlp</code> for format
                  options. For supported codecs and container formats, see{" "}
                  <code className="text-cyan-300">man ffmpeg-codecs</code> and{" "}
                  <code className="text-cyan-300">man ffmpeg-formats</code>.
                </p>
              </div>
              <div className="card-standard accent-green">
                <h4 className="text-green-300 font-semibold mb-2">
                  Media Processing & Batch Conversion
                </h4>
                <p className="text-gray-300 text-sm">
                  Batch convert images and videos to ASCII art with custom
                  palettes and dimensions. Process entire directories or create
                  frame sequences for animation
                </p>
              </div>
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Documentation & Creative Projects
                </h4>
                <p className="text-gray-300 text-sm">
                  Generate ASCII art screenshots for README files,
                  presentations, blog posts, or web pages. Create unique visual
                  content from videos or webcam feeds
                </p>
              </div>
              <div className="card-standard accent-teal">
                <h4 className="text-teal-300 font-semibold mb-2">
                  Automated Testing & CI/CD
                </h4>
                <p className="text-gray-300 text-sm">
                  Test that the entire video/crypto/network stack works by
                  checking exit codes and comparing frame output. Verify network
                  connectivity, encryption, and media handling in CI
                  environments without interactive display
                </p>
              </div>
              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">
                  Debug Network Issues
                </h4>
                <p className="text-gray-300 text-sm">
                  Quickly verify whether connectivity, encryption, server
                  response, or client setup is the problem. Single command
                  checks the entire pipeline
                </p>
              </div>
            </div>
          </section>

          <Footer />
        </div>
      </div>
    </>
  );
}
