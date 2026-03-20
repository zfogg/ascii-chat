import { Heading } from "@ascii-chat/shared/components";
import { CodeBlock } from "@ascii-chat/shared/components";

export default function RenderingExportSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-orange-400">
        🎥 Rendering & Export
      </Heading>

      <p className="docs-paragraph">
        Export ASCII art rendering to video files, GIFs, images, or stdout.
        Perfect for creating clips, GIFs for documentation, or piping to other
        tools:
      </p>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-orange-300 mb-3">
          Export to Video Files
        </Heading>
        <p className="docs-paragraph">
          Render ASCII art directly to MP4, WebM, MOV, AVI, or other video
          formats. Works with files, URLs, live streams, or webcam. Includes
          custom fonts, themes, and sizing:
        </p>
        <CodeBlock language="bash">
          {
            "# Export file to MP4 video\nascii-chat mirror --file video.mp4 --render-file output.mp4\n\n# Render YouTube video\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --render-file youtube-ascii.mp4\n\n# Render webcam to video\nascii-chat mirror --render-file webcam.mp4\n\n# Export to WebM\nascii-chat mirror --file movie.mkv --render-file output.webm\n\n# With custom font and size\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --render-file output.mp4 \\\n  --render-font 'JetBrains Mono' \\\n  --render-font-size 14\n\n# Matrix-style font (default)\nascii-chat mirror --file video.mp4 \\\n  --render-file output.mp4 \\\n  --render-font matrix\n\n# Render live stream to MP4\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' \\\n  --render-file livestream.mp4 --snapshot-delay 30"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-yellow-300 mb-3">
          Export to GIF
        </Heading>
        <p className="docs-paragraph">
          Create animated GIFs from ASCII rendering. Works with files, URLs,
          streams, or webcam. Great for README files and documentation:
        </p>
        <CodeBlock language="bash">
          {
            "# GIF from local file\nascii-chat mirror --file animation.mp4 --render-file output.gif\n\n# GIF from YouTube video\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --render-file demo.gif --snapshot-delay 5\n\n# GIF with specific dimensions\nascii-chat mirror --file video.mp4 \\\n  --render-file output.gif \\\n  --width 80 --height 24\n\n# GIF from live stream clip\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' \\\n  --render-file clip.gif --snapshot-delay 10\n\n# GIF from webcam\nascii-chat mirror --render-file webcam.gif --snapshot-delay 3"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          Export to Images
        </Heading>
        <p className="docs-paragraph">
          Capture a single frame as PNG or JPEG. Works with any input source
          (files, URLs, streams, webcam):
        </p>
        <CodeBlock language="bash">
          {
            "# Single frame from file to PNG\nascii-chat mirror --file video.mp4 \\\n  --snapshot --render-file frame.png\n\n# Webcam snapshot to PNG\nascii-chat mirror --snapshot --render-file webcam.png\n\n# YouTube at specific timestamp to JPEG\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --seek 30 --snapshot --render-file screenshot.jpg\n\n# Live stream frame to JPEG\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' \\\n  --snapshot --render-file livestream.jpg\n\n# With custom font\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --snapshot --render-file output.png \\\n  --render-font 'Courier New' --render-font-size 11\n\n# Light theme PNG\nascii-chat mirror --file image.jpg --snapshot \\\n  --render-file output.png --render-theme light"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-cyan-300 mb-3">
          Export to Stdout (MP4 Video Data)
        </Heading>
        <p className="docs-paragraph">
          Render ASCII art to MP4 video format on stdout using{" "}
          <code className="code-inline">--render-file '-'</code>. Perfect for
          piping to FFmpeg, saving to file, or streaming. Works with files,
          URLs, streams, or webcam:
        </p>
        <CodeBlock language="bash">
          {
            "# Render file and save as MP4\nascii-chat mirror --file video.mp4 \\\n  --snapshot --render-file '-' > output.mp4\n\n# Render YouTube video and save\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --snapshot --render-file '-' > youtube.mp4\n\n# Render webcam and save\nascii-chat mirror --snapshot --render-file '-' > webcam.mp4\n\n# Pipe MP4 to FFmpeg for further processing\nascii-chat mirror --file video.mp4 \\\n  --render-file '-' | ffmpeg -i pipe:0 -vf scale=1920x1080 output.mp4\n\n# Render live stream and save\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' \\\n  --snapshot --render-file '-' > stream.mp4\n\n# Render with custom dimensions\nascii-chat mirror --file video.mp4 \\\n  --width 200 --height 50 --render-file '-' > custom.mp4"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Advanced Workflows with FFmpeg
        </Heading>
        <p className="docs-paragraph">
          Combine ascii-chat rendering with FFmpeg for powerful media processing
          workflows:
        </p>
        <CodeBlock language="bash">
          {
            "# Render file → pipe to ffmpeg → convert format\nascii-chat mirror --file input.mp4 \\\n  --render-file '-' | ffmpeg -i pipe:0 -c:v libx264 output.webm\n\n# Render YouTube → ffmpeg → re-encode with custom codec\nascii-chat mirror --url 'https://youtu.be/VIDEO_ID' \\\n  --render-file '-' | ffmpeg -i pipe:0 \\\n  -c:v hevc_nvenc output.mp4\n\n# Render with custom font → ffmpeg → add audio overlay\nascii-chat mirror --file video.mp4 \\\n  --render-font 'JetBrains Mono' --render-font-size 14 \\\n  --render-file '-' | ffmpeg -i pipe:0 -i audio.mp3 \\\n  -c:v copy -c:a aac output.mp4\n\n# Render webcam → ffmpeg → streaming HLS output\nascii-chat mirror --render-file '-' | \\\nffmpeg -i pipe:0 -c:v libx264 \\\n  -f hls stream.m3u8\n\n# Render live stream → ffmpeg → archive with timestamp\nascii-chat mirror --url 'https://stream.example.com/live.m3u8' \\\n  --render-file '-' | ffmpeg -i pipe:0 \\\n  -c:v copy archive-$(date +%Y%m%d_%H%M%S).mp4\n\n# Render → ffmpeg → combine with other video\nascii-chat mirror --file video.mp4 --render-file '-' | \\\nffmpeg -i pipe:0 -i other_video.mp4 \\\n  -filter_complex '[0:v][1:v]hstack' combined.mp4"
          }
        </CodeBlock>
      </div>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-purple-300 mb-3">
          Rendering Options
        </Heading>
        <div className="space-y-3">
          <div className="card-standard accent-purple">
            <Heading level={4} className="text-purple-300 font-semibold mb-2">
              Theme
            </Heading>
            <p className="text-gray-300 text-sm mb-2">
              <code className="code-inline">
                --render-theme dark|light|auto
              </code>{" "}
              Color scheme for rendered output (default: dark)
            </p>
          </div>
          <div className="card-standard accent-pink">
            <Heading level={4} className="text-pink-300 font-semibold mb-2">
              Font
            </Heading>
            <p className="text-gray-300 text-sm mb-2">
              <code className="code-inline">--render-font NAME</code> Font to
              use (matrix, default, system fonts, or .ttf/.otf path)
            </p>
            <p className="text-gray-400 text-xs">
              Options: <code className="code-inline">matrix</code>,{" "}
              <code className="code-inline">default</code>, JetBrains Mono,
              Courier New, or path to custom font file
            </p>
          </div>
          <div className="card-standard accent-indigo">
            <Heading level={4} className="text-indigo-300 font-semibold mb-2">
              Font Size
            </Heading>
            <p className="text-gray-300 text-sm mb-2">
              <code className="code-inline">--render-font-size N</code> Size in
              points (default: 12.0, supports fractional sizes like 10.5)
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}
