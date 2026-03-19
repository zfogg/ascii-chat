import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function HTTPSnapshotsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-pink-400">
        🎥 HTTP/HTTPS, RTMP & YouTube Snapshots
      </Heading>

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
  );
}
