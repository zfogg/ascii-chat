import { Heading } from "@ascii-chat/shared/components";

export default function SupportedFormatsSection() {
  return (
    <section className="docs-section-spacing">
      <Heading level={2} className="heading-2 text-cyan-400">
        📁 Supported Media Formats
      </Heading>

      <div className="docs-subsection-spacing">
        <Heading level={3} className="heading-3 text-teal-300 mb-3">
          Video Formats
        </Heading>
        <p className="docs-paragraph">
          ASCII art rendering supports any video container and codec that
          FFmpeg supports:
        </p>
        <div className="space-y-3">
          <div className="card-standard accent-cyan">
            <Heading
              level={4}
              className="text-cyan-300 font-semibold mb-2"
            >
              Containers
            </Heading>
            <p className="text-gray-400 text-sm">
              MP4, WebM, MKV, MOV, AVI, FLV, 3GP, WMV, TS, M2TS, MTS, and
              more
            </p>
          </div>
          <div className="card-standard accent-teal">
            <Heading
              level={4}
              className="text-teal-300 font-semibold mb-2"
            >
              Video Codecs
            </Heading>
            <p className="text-gray-400 text-sm">
              H.264, H.265 (HEVC), VP8, VP9, AV1, MPEG-2, MPEG-4, Theora,
              and more
            </p>
          </div>
          <div className="card-standard accent-purple">
            <Heading
              level={4}
              className="text-purple-300 font-semibold mb-2"
            >
              Audio Codecs
            </Heading>
            <p className="text-gray-400 text-sm">
              AAC, MP3, Opus, Vorbis, FLAC, PCM, and more
            </p>
          </div>
          <div className="card-standard accent-green">
            <Heading
              level={4}
              className="text-green-300 font-semibold mb-2"
            >
              Image Formats
            </Heading>
            <p className="text-gray-400 text-sm">
              JPEG, PNG, WebP, GIF, BMP, TIFF, and more
            </p>
          </div>
        </div>
      </div>
    </section>
  );
}
