import { useEffect } from "react";
import Footer from "../../components/Footer";
import TrackedLink from "../../components/TrackedLink";
import { setBreadcrumbSchema } from "../../utils/breadcrumbs";

export default function Media() {
  useEffect(() => {
    setBreadcrumbSchema([
      { name: "Home", path: "/" },
      { name: "Documentation", path: "/docs" },
      { name: "Media Files and URLs", path: "/docs/media" },
    ]);
  }, []);
  return (
    <div className="bg-gray-950 text-gray-100 flex flex-col">
      <div className="flex-1 flex flex-col docs-container">
        <header className="mb-12 sm:mb-16">
          <h1 className="heading-1 mb-4">
            <span className="text-yellow-400">🎬</span> Media Files and URLs
          </h1>
          <p className="text-lg sm:text-xl text-gray-300">
            Stream local files, remote URLs, live streams, and piped media as
            ASCII art
          </p>
        </header>

        {/* Supported Media */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-cyan-400">
            📁 Supported Media Formats
          </h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">Video Formats</h3>
            <p className="docs-paragraph">
              ASCII art rendering supports any video container and codec that
              FFmpeg supports:
            </p>
            <div className="space-y-3">
              <div className="card-standard accent-cyan">
                <h4 className="text-cyan-300 font-semibold mb-2">Containers</h4>
                <p className="text-gray-400 text-sm">
                  MP4, WebM, MKV, MOV, AVI, FLV, 3GP, WMV, TS, M2TS, MTS, and
                  more
                </p>
              </div>
              <div className="card-standard accent-teal">
                <h4 className="text-teal-300 font-semibold mb-2">
                  Video Codecs
                </h4>
                <p className="text-gray-400 text-sm">
                  H.264, H.265 (HEVC), VP8, VP9, AV1, MPEG-2, MPEG-4, Theora,
                  and more
                </p>
              </div>
              <div className="card-standard accent-purple">
                <h4 className="text-purple-300 font-semibold mb-2">
                  Audio Codecs
                </h4>
                <p className="text-gray-400 text-sm">
                  AAC, MP3, Opus, Vorbis, FLAC, PCM, and more
                </p>
              </div>
              <div className="card-standard accent-green">
                <h4 className="text-green-300 font-semibold mb-2">
                  Image Formats
                </h4>
                <p className="text-gray-400 text-sm">
                  JPEG, PNG, WebP, GIF, BMP, TIFF, and more
                </p>
              </div>
            </div>
          </div>
        </section>

        {/* Local Files */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-green-400">💾 Local Files</h2>
          <p className="docs-paragraph">
            ascii-chat uses <strong>FFmpeg</strong> for media decoding and
            format conversion, and optionally <strong>yt-dlp</strong> for
            downloading streams. For detailed information on supported formats
            and codecs, see{" "}
            <code className="code-inline">man ffmpeg-formats</code>,{" "}
            <code className="code-inline">man ffmpeg-codecs</code>, and{" "}
            <code className="code-inline">man yt-dlp</code>.
          </p>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">
              Video Files (All Formats)
            </h3>
            <p className="docs-paragraph">
              Stream any video format from disk to ASCII art. Supports MP4, MKV,
              MOV, WebM, AVI, FLV, and more:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# MP4 video\nascii-chat mirror --file video.mp4\n\n# Matroska container (MKV)\nascii-chat mirror --file movie.mkv\n\n# Apple QuickTime (MOV)\nascii-chat mirror --file clip.mov\n\n# WebM format\nascii-chat mirror --file presentation.webm\n\n# AVI container\nascii-chat mirror --file oldvideo.avi\n\n# Custom playback settings\nascii-chat mirror --file video.mp4 --fps 30 --width 160 --height 40\n\n# Block characters (simpler, more artistic)\nascii-chat mirror --file video.mp4 --palette blocks\n\n# Loop playback with audio\nascii-chat mirror --file movie.mp4 --loop --audio"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">Animated GIFs</h3>
            <p className="docs-paragraph">
              Play animated GIF files directly with full control over playback:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Basic GIF playback (preserves original animation speed)\nascii-chat mirror --file animation.gif\n\n# GIF with custom frame rate (override timing)\nascii-chat mirror --file animation.gif --fps 24\n\n# Larger GIF display\nascii-chat mirror --file animation.gif --width 160 --height 40\n\n# Loop continuously\nascii-chat mirror --file animation.gif --loop\n\n# Block-style rendering for artistic effect\nascii-chat mirror --file animation.gif --palette blocks\n\n# With truecolor for quality\nascii-chat mirror --file animation.gif --color-mode truecolor"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-yellow-300 mb-3">Image Formats</h3>
            <p className="docs-paragraph">
              Display and process individual images. Supports PNG, JPEG, WebP,
              BMP, TIFF, and more:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# PNG images\nascii-chat mirror --file photo.png\n\n# JPEG images\nascii-chat mirror --file picture.jpg\n\n# WebP format\nascii-chat mirror --file image.webp\n\n# Bitmap format (BMP)\nascii-chat mirror --file bitmap.bmp\n\n# TIFF format\nascii-chat mirror --file scan.tiff\n\n# With custom dimensions\nascii-chat mirror --file image.png --width 140 --height 35\n\n# With truecolor for best quality\nascii-chat mirror --file photo.jpg --color-mode truecolor\n\n# Block-style rendering\nascii-chat mirror --file artwork.png --palette blocks"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-red-300 mb-3">
              Seeking & Timestamps
            </h3>
            <p className="docs-paragraph">
              Jump to specific timestamps in videos, audio, and streaming URLs
              using seconds:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Seek to 30 seconds into video\nascii-chat mirror --file video.mp4 --seek 30\n\n# Seek to 2 minutes 30 seconds (150 seconds total)\nascii-chat mirror --file movie.mkv --seek 150\n\n# Seek in YouTube video\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --seek 120\n\n# Seek in live HLS stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' --seek 0\n\n# Seek and take snapshot\nascii-chat mirror --file video.mp4 --seek 45 --snapshot > frame.txt\n\n# Seek with RTMP stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream' --seek 30"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-indigo-300 mb-3">Remote URLs</h3>
            <p className="docs-paragraph">
              Stream from HTTP/HTTPS, YouTube, and RTMP sources directly without
              downloading:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# HTTP/HTTPS media file\nascii-chat mirror --url 'https://example.com/video.mp4'\n\n# YouTube video (requires yt-dlp)\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --audio\n\n# YouTube with truecolor quality\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --color-mode truecolor\n\n# HLS Live Stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8'\n\n# RTMP Stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream'\n\n# DASH Manifest\nascii-chat mirror --url 'https://stream.example.com/manifest.mpd'\n\n# Remote file with seek and snapshot\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --seek 60 --snapshot --color-mode truecolor > frame.txt"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Broadcast to Multiple Clients */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-blue-400">
            📡 Broadcasting to Clients
          </h2>

          <p className="docs-paragraph">
            Run a server streaming media, and have multiple clients view
            simultaneously:
          </p>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-blue-300 mb-3">
              Server: Stream Local File
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Server: Mirror local file to all connected clients\nascii-chat server --file movie.mp4\n\n# Server: With custom settings\nascii-chat server --file movie.mp4 --fps 30 --width 160 --audio\n\n# Server: On specific port with password\nascii-chat server --file movie.mp4 --port 8080 --password 'watch123'\n\n# Server: Register with ACDS (memorable session string)\nascii-chat server --file movie.mp4 --acds --password 'watch123'\n\n# Server: Stream file repeatedly\nascii-chat server --file movie.mp4 --loop --acds"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              Server: Stream Remote URL
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Server: Stream YouTube to all clients\nascii-chat server --url 'https://youtu.be/7ynHVGCehoM' --acds\n\n# Server: Stream live HLS broadcast\nascii-chat server --url 'https://stream.example.com/live.m3u8' \\\n  --fps 15 --acds --password 'live123'\n\n# Server: Stream with high quality\nascii-chat server --url 'https://example.com/video.mp4' \\\n  --color-mode truecolor --acds"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">
              Client: Watch Broadcast
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Connect to local broadcast via IP\nascii-chat client 192.168.1.50:27224\n\n# Connect with password\nascii-chat client --password 'watch123' 192.168.1.50\n\n# Join via ACDS session string\nascii-chat --password 'watch123' happy-movie-night\n\n# Adjust for slow network\nascii-chat --fps 15 --color-mode mono happy-movie-night\n\n# Save broadcast to file\nascii-chat --snapshot happy-movie-night > recording.txt"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* URLs */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-purple-400">
            🌐 Remote URLs & Streams
          </h2>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              HTTP/HTTPS Media Files
            </h3>
            <p className="docs-paragraph">
              Stream remote video files via HTTP/HTTPS. No local download
              needed, streams directly from URL:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Stream remote MP4\nascii-chat mirror --url https://example.com/video.mp4\n\n# Stream with specific dimensions\nascii-chat mirror --url https://cdn.example.com/large_video.mp4 \\\n  --width 200 --height 50\n\n# Stream with frame rate control\nascii-chat mirror --url https://example.com/video.mov --fps 24\n\n# Progressive download (resume friendly)\nascii-chat mirror --url https://example.com/recording.webm"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-yellow-300 mb-3">YouTube Videos</h3>
            <p className="docs-paragraph">
              Stream YouTube videos directly to terminal as ASCII art (requires
              yt-dlp):
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Basic YouTube streaming\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM'\n\n# With truecolor for best quality\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --color-mode truecolor\n\n# With audio passthrough\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --audio\n\n# Jump to specific timestamp (in seconds)\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' --seek 120\n\n# Lower resolution for slow networks\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --width 80 --height 24 --fps 15 --color-mode 16\n\n# Snapshot at specific time\nascii-chat mirror --url 'https://youtu.be/7ynHVGCehoM' \\\n  --seek 60 --snapshot --snapshot-delay 0 --color-mode truecolor > frame.txt"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">Live Streams</h3>
            <p className="docs-paragraph">
              Stream from HLS, DASH, RTMP, and other live protocols:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# HLS Live Stream\nascii-chat mirror --url 'https://stream.example.com/live.m3u8'\n\n# DASH Manifest\nascii-chat mirror --url 'https://stream.example.com/manifest.mpd'\n\n# RTMP Stream\nascii-chat mirror --url 'rtmp://live.example.com/app/stream'\n\n# RTMP with stream key\nascii-chat mirror --url 'rtmp://broadcaster.example.com/live/mykey'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-orange-300 mb-3">
              Seeking in Remote Media
            </h3>
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
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Skip to 1 minute\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 60\n\n# Jump to 1 minute 45 seconds\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 105\n\n# Snapshot at specific time\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' --seek 30 --snapshot --snapshot-delay 0"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Piping & FFmpeg */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-green-400">
            🔄 Piping & FFmpeg Integration
          </h2>

          <p className="docs-paragraph">
            Pipe raw video frames from FFmpeg for advanced streaming scenarios.
            FFmpeg handles any format conversion, and ascii-chat renders to
            ASCII:
          </p>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">Webcam Streaming</h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Linux: V4L2 webcam (direct piping)\nffmpeg -f v4l2 -i /dev/video0 \\\n  -vf 'format=rgb24,scale=160:40' -f rawvideo - | \\\n  ascii-chat mirror -f '-' --width 160 --height 40 --fps 30\n\n# Windows: DirectShow (built-in camera)\nffmpeg -f dshow -i video='Built-in Camera' \\\n  -vf 'format=rgb24,scale=160:40' -f rawvideo - | \\\n  ascii-chat mirror -f '-'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-cyan-300 mb-3">
              Screen & Desktop Capture
            </h3>
            <p className="docs-paragraph">
              Stream your entire screen or specific window:
            </p>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Linux: Full screen capture (X11)\nffmpeg -f x11grab -i :0 \\\n  -vf 'format=rgb24,scale=200:50' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Linux: Specific window (wayland)\nffmpeg -f gdigrab -i desktop \\\n  -vf 'format=rgb24,scale=160:40' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Specific region (crop)\nffmpeg -f x11grab -i :0.0+100,100 \\\n  -vf 'format=rgb24,scale=160:40' \\\n  -f rawvideo - | ascii-chat mirror -f '-'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-teal-300 mb-3">
              Format Conversion & Streaming
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Convert video and stream\nffmpeg -i input.mov -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-' --width 160 --height 40\n\n# Slow down video for ASCII processing\nffmpeg -i video.mp4 -vf 'setpts=2*PTS' -pix_fmt rgb24 \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 15\n\n# Speed up video\nffmpeg -i video.mp4 -vf 'setpts=0.5*PTS' -pix_fmt rgb24 \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 60"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-indigo-300 mb-3">Images via Stdin</h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Convert image to RGB24 and pipe\nffmpeg -i image.png -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-'\n\n# Multiple images with ffmpeg\nffmpeg -i photo1.jpg -i photo2.jpg -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-pink-300 mb-3">
              Processing & Effects
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Add watermark or overlay\nffmpeg -i video.mp4 -i watermark.png \\\n  -filter_complex 'overlay=10:10' \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Rotate video\nffmpeg -i video.mp4 -vf 'rotate=90,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Mirror/flip horizontally\nffmpeg -i video.mp4 -vf 'hflip,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Extract frames every 2 seconds\nffmpeg -i video.mp4 -vf 'fps=0.5,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'"
                }
              </code>
            </pre>
          </div>

          <div className="docs-subsection-spacing">
            <h3 className="heading-3 text-green-300 mb-3">
              Live Stream Processing
            </h3>
            <pre className="code-block">
              <code className="code-content">
                {
                  "# Receive and display RTMP stream\nffmpeg -i rtmp://broadcaster/stream \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Receive HLS stream\nffmpeg -i https://stream.example.com/live.m3u8 \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Receive and scale\nffmpeg -i https://youtu.be/VIDEO_ID \\\n  -vf 'scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 30"
                }
              </code>
            </pre>
          </div>
        </section>

        {/* Advanced Options */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-teal-400">⚙️ Advanced Options</h2>

          <div className="space-y-3">
            <div className="card-standard accent-cyan">
              <h4 className="text-cyan-300 font-semibold mb-2">
                Frame Rate Control
              </h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--fps N</code> Set target frame
                rate (default: 30)
              </p>
              <p className="text-gray-400 text-xs">
                Example: <code className="code-inline">--fps 60</code> for
                smooth playback, <code className="code-inline">--fps 10</code>{" "}
                for bandwidth-constrained networks
              </p>
            </div>
            <div className="card-standard accent-purple">
              <h4 className="text-purple-300 font-semibold mb-2">
                Audio Passthrough
              </h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--audio</code> Enable audio from
                media file
              </p>
              <p className="text-gray-400 text-xs">
                Audio uses Opus codec for efficient low-bandwidth streaming
              </p>
            </div>
            <div className="card-standard accent-green">
              <h4 className="text-green-300 font-semibold mb-2">
                Loop Playback
              </h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--loop</code> Repeat media
                playback continuously
              </p>
              <p className="text-gray-400 text-xs">
                Useful for presentations or background displays
              </p>
            </div>
            <div className="card-standard accent-teal">
              <h4 className="text-teal-300 font-semibold mb-2">Scaling</h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--width, --height</code> Control
                display dimensions
              </p>
              <p className="text-gray-400 text-xs">
                Example:{" "}
                <code className="code-inline">--width 200 --height 50</code>
                for widescreen displays
              </p>
            </div>
            <div className="card-standard accent-orange">
              <h4 className="text-orange-300 font-semibold mb-2">Palettes</h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--palette blocks</code> Choose
                character set for rendering
              </p>
              <p className="text-gray-400 text-xs">
                Options: <code className="code-inline">detailed</code>,
                <code className="code-inline">blocks</code>,
                <code className="code-inline">minimal</code>
              </p>
            </div>
            <div className="card-standard accent-pink">
              <h4 className="text-pink-300 font-semibold mb-2">Seeking</h4>
              <p className="text-gray-300 text-sm mb-2">
                <code className="code-inline">--seek SECONDS</code> Jump to
                specific timestamp
              </p>
              <p className="text-gray-400 text-xs">
                Works with local files and HTTP progressive downloads
              </p>
            </div>
          </div>
        </section>

        {/* Use Cases */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-yellow-400">💼 Real-World Use Cases</h2>
          <div className="space-y-3">
            <div className="card-standard accent-cyan">
              <h4 className="text-cyan-300 font-semibold mb-2">
                Movie Night in Terminal
              </h4>
              <p className="text-gray-300 text-sm">
                Stream movies as ASCII art during development sessions or as a
                novelty. Works over SSH for remote viewing.
              </p>
            </div>
            <div className="card-standard accent-green">
              <h4 className="text-green-300 font-semibold mb-2">
                Live Presentations
              </h4>
              <p className="text-gray-300 text-sm">
                Stream live event feeds, screen recordings, or presentation
                videos to all participants simultaneously
              </p>
            </div>
            <div className="card-standard accent-purple">
              <h4 className="text-purple-300 font-semibold mb-2">
                Documentation & Tutorials
              </h4>
              <p className="text-gray-300 text-sm">
                Create ASCII art clips for README files, blog posts, or tutorial
                documentation. Capture specific moments with seek
              </p>
            </div>
            <div className="card-standard accent-teal">
              <h4 className="text-teal-300 font-semibold mb-2">
                Screen Recording Playback
              </h4>
              <p className="text-gray-300 text-sm">
                Replay screen recordings as ASCII art. Great for debugging
                sessions or demonstrating terminal-based workflows
              </p>
            </div>
            <div className="card-standard accent-orange">
              <h4 className="text-orange-300 font-semibold mb-2">
                YouTube Integration
              </h4>
              <p className="text-gray-300 text-sm">
                Stream YouTube videos directly to terminal. Combine with
                <code className="code-inline">--seek</code> for specific clips
              </p>
            </div>
            <div className="card-standard accent-pink">
              <h4 className="text-pink-300 font-semibold mb-2">
                Live Streaming Events
              </h4>
              <p className="text-gray-300 text-sm">
                Connect to live streams (HLS, DASH, RTMP) and display in
                terminal. Works over satellite, cellular, or constrained
                networks
              </p>
            </div>
            <div className="card-standard accent-yellow">
              <h4 className="text-yellow-300 font-semibold mb-2">
                Automated Demonstrations
              </h4>
              <p className="text-gray-300 text-sm">
                Pipe FFmpeg processing chains for automated video analysis or
                real-time monitoring dashboards in the terminal
              </p>
            </div>
          </div>
        </section>

        {/* Performance Tips */}
        <section className="docs-section-spacing">
          <h2 className="heading-2 text-cyan-400">💡 Performance Tips</h2>
          <div className="space-y-3">
            <div className="card-standard accent-cyan">
              <h4 className="text-cyan-300 font-semibold mb-2">
                Optimize for Network
              </h4>
              <p className="text-gray-400 text-sm">
                Reduce <code className="code-inline">--fps</code> and use lower
                <code className="code-inline">--color-mode</code> on slow
                connections. Mono rendering is fastest.
              </p>
            </div>
            <div className="card-standard accent-purple">
              <h4 className="text-purple-300 font-semibold mb-2">
                Bandwidth Usage
              </h4>
              <p className="text-gray-400 text-sm">
                Typical usage: monochrome at 10 fps ≈ 10-20 Kbps, truecolor at
                30 fps ≈ 200-500 Kbps. Add audio for +50-100 Kbps.
              </p>
            </div>
            <div className="card-standard accent-green">
              <h4 className="text-green-300 font-semibold mb-2">CPU Usage</h4>
              <p className="text-gray-400 text-sm">
                SIMD-accelerated ASCII conversion (1-4x speedup). Smaller output
                dimensions = lower CPU. Run on weak hardware with
                <code className="code-inline">--width 80 --height 24</code>.
              </p>
            </div>
          </div>
        </section>

        <Footer />
      </div>
    </div>
  );
}
