import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function LocalFilesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        💾 Local Files
      </Heading>
      <p className="docs-paragraph">
        ascii-chat uses <strong>FFmpeg</strong> for media decoding and format
        conversion, and optionally <strong>yt-dlp</strong> for downloading
        streams. For detailed information on supported formats and codecs, see{" "}
        <code className="code-inline">man ffmpeg-formats</code>,{" "}
        <code className="code-inline">man ffmpeg-codecs</code>, and{" "}
        <code className="code-inline">man yt-dlp</code>.
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Video Files (All Formats)
        </Heading>
        <p className="docs-paragraph">
          Stream any video format from disk to ASCII art. Supports MP4, MKV,
          MOV, WebM, AVI, FLV, and more:
        </p>
        <CodeBlock language="bash">
          {
            "# MP4 video\nascii-chat mirror --file video.mp4\n\n# Matroska container (MKV)\nascii-chat mirror --file movie.mkv\n\n# Apple QuickTime (MOV)\nascii-chat mirror --file clip.mov\n\n# WebM format\nascii-chat mirror --file presentation.webm\n\n# AVI container\nascii-chat mirror --file oldvideo.avi\n\n# Custom playback settings\nascii-chat mirror --file video.mp4 --fps 30 --width 160 --height 40\n\n# Block characters (simpler, more artistic)\nascii-chat mirror --file video.mp4 --palette blocks\n\n# Loop playback with audio\nascii-chat mirror --file movie.mp4 --loop --audio"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Animated GIFs
        </Heading>
        <p className="docs-paragraph">
          Play animated GIF files directly with full control over playback:
        </p>
        <CodeBlock language="bash">
          {
            "# Basic GIF playback (preserves original animation speed)\nascii-chat mirror --file animation.gif\n\n# GIF with custom frame rate (override timing)\nascii-chat mirror --file animation.gif --fps 24\n\n# Larger GIF display\nascii-chat mirror --file animation.gif --width 160 --height 40\n\n# Loop continuously\nascii-chat mirror --file animation.gif --loop\n\n# Block-style rendering for artistic effect\nascii-chat mirror --file animation.gif --palette blocks\n\n# With truecolor for quality\nascii-chat mirror --file animation.gif --color-mode truecolor"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          Image Formats
        </Heading>
        <p className="docs-paragraph">
          Display and process individual images. Supports PNG, JPEG, WebP, BMP,
          TIFF, and more:
        </p>
        <CodeBlock language="bash">
          {
            "# PNG images\nascii-chat mirror --file photo.png\n\n# JPEG images\nascii-chat mirror --file picture.jpg\n\n# WebP format\nascii-chat mirror --file image.webp\n\n# Bitmap format (BMP)\nascii-chat mirror --file bitmap.bmp\n\n# TIFF format\nascii-chat mirror --file scan.tiff\n\n# With custom dimensions\nascii-chat mirror --file image.png --width 140 --height 35\n\n# With truecolor for best quality\nascii-chat mirror --file photo.jpg --color-mode truecolor\n\n# Block-style rendering\nascii-chat mirror --file artwork.png --palette blocks"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-red-300 mb-3">
          Seeking & Timestamps
        </Heading>
        <p className="docs-paragraph">
          Jump to specific timestamps in videos, audio, and streaming URLs using
          seconds:
        </p>
        <CodeBlock language="bash">
          {
            "# Seek to 30 seconds into video\nascii-chat mirror --file video.mp4 --seek 30\n\n# Seek to 2 minutes 30 seconds (150 seconds total)\nascii-chat mirror --file movie.mkv --seek 150\n\n# Seek in YouTube video\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --seek 120\n\n# Seek in live HLS stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' --seek 0\n\n# Seek and take snapshot\nascii-chat mirror --file video.mp4 --seek 45 --snapshot > frame.txt\n\n# Seek with RTMP stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream' --seek 30"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-indigo-300 mb-3">
          Remote URLs
        </Heading>
        <p className="docs-paragraph">
          Stream from HTTP/HTTPS, YouTube, and RTMP sources directly without
          downloading:
        </p>
        <CodeBlock language="bash">
          {
            "# HTTP/HTTPS media file\nascii-chat mirror --url 'https://example.com/video.mp4'\n\n# YouTube video (requires yt-dlp)\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --audio\n\n# YouTube with truecolor quality\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --color-mode truecolor\n\n# HLS Live Stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8'\n\n# RTMP Stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream'\n\n# DASH Manifest\nascii-chat mirror --url 'https://stream.example.com/manifest.mpd'\n\n# Remote file with seek and snapshot\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --seek 60 --snapshot --color-mode truecolor > frame.txt"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
