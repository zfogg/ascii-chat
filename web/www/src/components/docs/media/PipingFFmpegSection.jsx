import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components/CodeBlock";

export default function PipingFFmpegSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-green-400">
        🔄 Piping & FFmpeg Integration
      </Heading>

      <p className="docs-paragraph">
        Pipe raw video frames from FFmpeg for advanced streaming scenarios.
        FFmpeg handles any format conversion, and ascii-chat renders to ASCII:
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Webcam Streaming
        </Heading>
        <CodeBlock language="bash">
          {
            "# Linux: V4L2 webcam (direct piping)\nffmpeg -f v4l2 -i /dev/video0 \\\n  -vf 'format=rgb24,scale=160:40' -f rawvideo - | \\\n  ascii-chat mirror -f '-' --width 160 --height 40 --fps 30\n\n# Windows: DirectShow (built-in camera)\nffmpeg -f dshow -i video='Built-in Camera' \\\n  -vf 'format=rgb24,scale=160:40' -f rawvideo - | \\\n  ascii-chat mirror -f '-'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Screen & Desktop Capture
        </Heading>
        <p className="docs-paragraph">
          Stream your entire screen or specific window:
        </p>
        <CodeBlock language="bash">
          {
            "# Linux: Full screen capture (X11)\nffmpeg -f x11grab -i :0 \\\n  -vf 'format=rgb24,scale=200:50' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Specific region (crop)\nffmpeg -f x11grab -i :0.0+100,100 \\\n  -vf 'format=rgb24,scale=160:40' \\\n  -f rawvideo - | ascii-chat mirror -f '-'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Format Conversion & Streaming
        </Heading>
        <CodeBlock language="bash">
          {
            "# Convert video and stream\nffmpeg -i input.mov -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-' --width 160 --height 40\n\n# Slow down video for ASCII processing\nffmpeg -i video.mp4 -vf 'setpts=2*PTS' -pix_fmt rgb24 \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 15\n\n# Speed up video\nffmpeg -i video.mp4 -vf 'setpts=0.5*PTS' -pix_fmt rgb24 \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 60"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-indigo-300 mb-3">
          Images via Stdin
        </Heading>
        <CodeBlock language="bash">
          {
            "# Convert image to RGB24 and pipe\nffmpeg -i image.png -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-'\n\n# Multiple images with ffmpeg\nffmpeg -i photo1.jpg -i photo2.jpg -pix_fmt rgb24 -f rawvideo - | \\\n  ascii-chat mirror -f '-'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-pink-300 mb-3">
          Processing & Effects
        </Heading>
        <CodeBlock language="bash">
          {
            "# Add watermark or overlay\nffmpeg -i video.mp4 -i watermark.png \\\n  -filter_complex 'overlay=10:10' \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Rotate video\nffmpeg -i video.mp4 -vf 'rotate=90,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Mirror/flip horizontally\nffmpeg -i video.mp4 -vf 'hflip,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'\n\n# Extract frames every 2 seconds\nffmpeg -i video.mp4 -vf 'fps=0.5,scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-'"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Live Stream Processing
        </Heading>
        <CodeBlock language="bash">
          {
            "# Receive and display RTMP stream\nffmpeg -i rtmp://broadcaster/stream \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Receive HLS stream\nffmpeg -i https://stream.example.com/live.m3u8 \\\n  -pix_fmt rgb24 -f rawvideo - | ascii-chat mirror -f '-'\n\n# Receive and scale\nffmpeg -i https://youtu.be/VIDEO_ID \\\n  -vf 'scale=160:40,format=rgb24' \\\n  -f rawvideo - | ascii-chat mirror -f '-' --fps 30"
          }
        </CodeBlock>
      </div>
    </section>
  );
}
