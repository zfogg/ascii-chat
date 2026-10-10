import {
  VideoUploadEncoder,
  type CompressedVideoEncoding,
  type VideoChunk,
} from "./VideoUploadEncoder";
export type VideoEncoding = "auto" | CompressedVideoEncoding | "raw";

/** Explicit HEVC requests must never silently fall back to raw pixels. */
export function getVideoEncoding(
  search = window.location.search,
): VideoEncoding {
  const value = new URLSearchParams(search).get("encoding")?.toLowerCase();
  if (value === undefined || value === "auto") return "auto";
  if (value === "hevc" || value === "hvec" || value === "h265") return "hevc";
  if (["h264", "h.264", "avc", "x264"].includes(value ?? "")) return "h264";
  if (value === "raw") return "raw";
  throw new Error(
    `Unknown encoding "${value}". Use ?encoding=hevc, ?encoding=H.264, or ?encoding=raw.`,
  );
}

/** Probe in preference order. Explicit selections never fall back. */
export async function selectVideoEncoder(
  encoding: VideoEncoding,
  width: number,
  height: number,
  fps: number,
  output: (chunk: VideoChunk, codec: CompressedVideoEncoding) => void,
  after?: CompressedVideoEncoding,
): Promise<VideoUploadEncoder | null> {
  const candidates: CompressedVideoEncoding[] =
    encoding === "auto"
      ? after === "h264"
        ? []
        : after === "hevc"
          ? ["h264"]
          : ["hevc", "h264"]
      : encoding === "raw"
        ? []
        : [encoding];
  for (const codec of candidates) {
    const encoder = new VideoUploadEncoder(
      (chunk) => output(chunk, codec),
      codec,
    );
    try {
      await encoder.initialize(width, height, fps);
      return encoder;
    } catch (error) {
      encoder.destroy();
      if (encoding !== "auto") throw error;
      console.info(
        `[Client] ${encoder.label} unavailable; trying next encoding`,
        error,
      );
    }
  }
  return null;
}
