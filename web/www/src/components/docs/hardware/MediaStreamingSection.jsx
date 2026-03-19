import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function MediaStreamingSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-indigo-400">
        🎥 Media Streaming
      </Heading>

      <p className="docs-paragraph mb-6">
        Stream from local files or URLs instead of (or alongside) webcam
        capture. Works in client and mirror modes.
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-indigo-300 mb-3">
          Stream Local Files
        </Heading>
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
        <Heading level={3} className="heading-3 text-blue-300 mb-3">
          Stream from URLs
        </Heading>
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
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Playback Control
        </Heading>
        <CodeBlock language="bash">
          {
            "# Loop file playback\nascii-chat mirror -f video.mp4 -l\n\n# Start playback paused (toggle with spacebar)\nascii-chat client example.com -f video.mp4 --pause\n\n# Seek to specific timestamp\nascii-chat mirror -f video.mp4 -s 22:10\nascii-chat mirror -f video.mp4 -s 00:30  # MM:SS format\nascii-chat mirror -f video.mp4 -s 1:23:45  # HH:MM:SS format\n\n# Start at specific time and immediately exit (snapshot)\nascii-chat mirror -f video.mp4 -s 5:12 -S -D 0"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          YouTube & Cookies
        </Heading>
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
  );
}
