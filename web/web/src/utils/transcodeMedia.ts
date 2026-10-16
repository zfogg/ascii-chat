import { FFmpeg } from "@ffmpeg/ffmpeg";
import coreURL from "@ffmpeg/core?url";
import wasmURL from "@ffmpeg/core/wasm?url";

/** Lazy, same-origin FFmpeg worker; no media leaves the browser. */
export async function transcodeMedia(
  file: File,
  signal: AbortSignal,
  status: (message: string) => void,
) {
  if (file.size > 256 * 1024 * 1024)
    throw new Error(
      "Browser conversion is limited to 256 MB. Convert this file to MP4 or WebM locally first.",
    );
  signal.throwIfAborted();
  const ffmpeg = new FFmpeg();
  const cancel = () => ffmpeg.terminate();
  signal.addEventListener("abort", cancel, { once: true });
  try {
    status("Loading video converter…");
    await ffmpeg.load({ coreURL, wasmURL });
    signal.throwIfAborted();
    status("Converting video and audio…");
    const input = new Uint8Array(await file.arrayBuffer());
    signal.throwIfAborted();
    await ffmpeg.writeFile("input", input);
    const result = await ffmpeg.exec([
      "-i",
      "input",
      "-map",
      "0:v:0",
      "-map",
      "0:a:0?",
      "-vf",
      "scale=w='min(1280,iw)':h='min(720,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2",
      "-c:v",
      "libx264",
      "-preset",
      "ultrafast",
      "-crf",
      "23",
      "-pix_fmt",
      "yuv420p",
      "-c:a",
      "aac",
      "-b:a",
      "128k",
      "-ac",
      "2",
      "-movflags",
      "+faststart",
      "output.mp4",
    ]);
    if (result !== 0)
      throw new Error(
        "FFmpeg could not decode this file's video/audio tracks.",
      );
    const output = await ffmpeg.readFile("output.mp4");
    if (typeof output === "string")
      throw new Error("Converter returned no video data.");
    return new Blob([new Uint8Array(output)], { type: "video/mp4" });
  } finally {
    signal.removeEventListener("abort", cancel);
    ffmpeg.terminate();
  }
}
