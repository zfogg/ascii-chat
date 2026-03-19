import { Heading } from "@ascii-chat/shared/components";

export default function UseCasesSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-teal-400">
        💼 Real-World Use Cases
      </Heading>
      <div className="space-y-3">
        <div className="card-standard accent-yellow">
          <Heading
            level={4}
            className="text-yellow-300 font-semibold mb-2"
          >
            YouTube, RTMP & Streaming Media
          </Heading>
          <p className="text-gray-300 text-sm mb-2">
            Capture ASCII art frames from any media source—YouTube videos,
            HTTP(S) files, RTMP live streams, and more. Use{" "}
            <code className="text-cyan-300">--url</code> and{" "}
            <code className="text-cyan-300">--seek</code> to grab frames
            at specific timestamps for documentation, analysis, or
            creative projects. Combine{" "}
            <code className="text-cyan-300">--seek</code> with{" "}
            <code className="text-cyan-300">--snapshot-delay</code> to
            save ASCII art segments from URLs or media files.
          </p>
          <p className="text-gray-400 text-xs">
            <strong>Tip:</strong> Use{" "}
            <a
              href="https://github.com/yt-dlp/yt-dlp"
              target="_blank"
              rel="noopener noreferrer"
              className="text-cyan-300 hover:text-cyan-200 underline"
            >
              yt-dlp
            </a>{" "}
            to download videos first, then snapshot locally. See{" "}
            <code className="text-cyan-300">man yt-dlp</code> for format
            options. For supported codecs and container formats, see{" "}
            <code className="text-cyan-300">man ffmpeg-codecs</code> and{" "}
            <code className="text-cyan-300">man ffmpeg-formats</code>.
          </p>
        </div>
        <div className="card-standard accent-green">
          <Heading
            level={4}
            className="text-green-300 font-semibold mb-2"
          >
            Media Processing & Batch Conversion
          </Heading>
          <p className="text-gray-300 text-sm">
            Batch convert images and videos to ASCII art with custom
            palettes and dimensions. Process entire directories or create
            frame sequences for animation
          </p>
        </div>
        <div className="card-standard accent-purple">
          <Heading
            level={4}
            className="text-purple-300 font-semibold mb-2"
          >
            Documentation & Creative Projects
          </Heading>
          <p className="text-gray-300 text-sm">
            Generate ASCII art screenshots for README files,
            presentations, blog posts, or web pages. Create unique visual
            content from videos or webcam feeds
          </p>
        </div>
        <div className="card-standard accent-teal">
          <Heading level={4} className="text-teal-300 font-semibold mb-2">
            Scripting and Testing
          </Heading>
          <p className="text-gray-300 text-sm">
            The developer uses it to test that ascii-chat works as he
            builds it
          </p>
        </div>
        <div className="card-standard accent-cyan">
          <Heading level={4} className="text-cyan-300 font-semibold mb-2">
            🤳 Take an ASCII Art Selfie
          </Heading>
          <p className="text-gray-300 text-sm">
            Capture yourself as ASCII art. Perfect for profile pictures,
            social media, or just having fun. Use zero delay for an
            instant snapshot
          </p>
        </div>
      </div>
    </section>
  );
}
