import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";
import TrackedLink from "../../TrackedLink";

export default function RemoteURLsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-purple-400">
        🌐 Remote URLs & Streams
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          HTTP/HTTPS Media Files
        </Heading>
        <p className="docs-paragraph">
          Stream remote video files via HTTP/HTTPS. No local download needed,
          streams directly from URL:
        </p>
        <CodeBlock language="bash">
          {
            "# Stream remote MP4\nascii-chat mirror --url https://example.com/video.mp4\n\n# Stream with specific dimensions\nascii-chat mirror --url https://cdn.example.com/large_video.mp4 \\\n  --width 200 --height 50\n\n# Stream with frame rate control\nascii-chat mirror --url https://example.com/video.mov --fps 24\n\n# Progressive download (resume friendly)\nascii-chat mirror --url https://example.com/recording.webm"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          YouTube Videos
        </Heading>
        <p className="docs-paragraph">
          Stream YouTube videos directly to terminal as ASCII art (requires
          yt-dlp):
        </p>
        <CodeBlock language="bash">
          {
            "# Basic YouTube streaming\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM'\n\n# With truecolor for best quality\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --color-mode truecolor\n\n# With audio passthrough\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --audio\n\n# Jump to specific timestamp (in seconds)\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --seek 120\n\n# Lower resolution for slow networks\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --width 80 --height 24 --fps 15 --color-mode 16\n\n# Snapshot at specific time\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --seek 60 --snapshot --snapshot-delay 0 --color-mode truecolor > frame.txt"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Live Streams
        </Heading>
        <p className="docs-paragraph">
          Stream from HLS, DASH, RTMP, and other live protocols:
        </p>
        <CodeBlock language="bash">
          {
            "# HLS Live Stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8'\n\n# DASH Manifest\nascii-chat mirror --url 'https://stream.example.com/manifest.mpd'\n\n# RTMP Stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream'\n\n# RTMP with stream key\nascii-chat mirror --url 'rtmp://broadcaster.example.com/live/mykey'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Seeking in Remote Media
        </Heading>
        <p className="docs-paragraph">
          Jump to specific timestamps (works with files,{" "}
          <TrackedLink
            to="/docs/snapshot"
            label="snapshot mode"
            className="text-cyan-400 hover:text-cyan-300 transition-colors"
          >
            snapshots
          </TrackedLink>
          , live streams):
        </p>
        <CodeBlock language="bash">
          {
            "# Skip to 1 minute\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 60\n\n# Jump to 1 minute 45 seconds\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 105\n\n# Snapshot at specific time\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 30 --snapshot --snapshot-delay 0"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
