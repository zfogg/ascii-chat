import { MAX_MEDIA_FILE_SIZE } from "./mediaUpload";
import { FFmpeg } from "@ffmpeg/ffmpeg";
import coreURL from "@ffmpeg/core?url";
import wasmURL from "@ffmpeg/core/wasm?url";
/** Lazy, same-origin FFmpeg worker; no media leaves the browser. */
export async function transcodeMedia(
  file: File,
  signal: AbortSignal,
  status: (message: string) => void,
  allowNative = false,
) {
  if (file.size > MAX_MEDIA_FILE_SIZE)
    throw new Error(
      "Browser media inspection and conversion are limited to 8 GB. Trim or compress this file locally first.",
    );
  signal.throwIfAborted();
  const ffmpeg = new FFmpeg();
  const cancel = () => ffmpeg.terminate();
  signal.addEventListener("abort", cancel, { once: true });
  try {
    status("Loading video converter...");
    await ffmpeg.load({ coreURL, wasmURL });
    signal.throwIfAborted();
    const input = new Uint8Array(await file.arrayBuffer());
    signal.throwIfAborted();
    await ffmpeg.writeFile("input", input);
    if (allowNative) {
      status("Checking audio compatibility...");
      const probe = await ffmpeg.ffprobe([
        "-v",
        "error",
        "-select_streams",
        "a:0",
        "-show_entries",
        "stream=codec_name,profile",
        "-of",
        "json",
        "input",
        "-o",
        "probe.json",
      ]);
      if (probe === 0) {
        const data = await ffmpeg.readFile("probe.json");
        const info = JSON.parse(
          typeof data === "string" ? data : new TextDecoder().decode(data),
        ) as {
          streams?: { codec_name?: string; profile?: string }[];
        };
        const audio = info.streams?.[0];
        const types: Record<string, string> = {
          opus: 'audio/webm; codecs="opus"',
          vorbis: 'audio/webm; codecs="vorbis"',
          mp3: "audio/mpeg",
        };
        const type =
          audio?.codec_name === "aac" && audio.profile === "LC"
            ? 'audio/mp4; codecs="mp4a.40.2"'
            : types[audio?.codec_name ?? ""];
        if (
          info.streams &&
          (!audio ||
            (type &&
              document.createElement("audio").canPlayType(type) === "probably"))
        )
          return file;
      }
    }
    status("Converting video and audio...");
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
