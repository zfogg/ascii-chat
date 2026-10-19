import type { MediaProbe } from "./mediaBuffer";

// The pinned FFmpeg core exposes Emscripten's filesystem. A non-seekable device
// forwards muxer writes while exec() is still running on this worker.
interface Core {
  ret: number;
  exec: (...args: string[]) => void;
  ffprobe: (...args: string[]) => void;
  reset: () => void;
  setTimeout: (timeout: number) => void;
  setLogger: (callback: (entry: { message: string }) => void) => void;
  FS: {
    writeFile: (path: string, data: Uint8Array) => void;
    readFile: (path: string, options: { encoding: "utf8" }) => string;
    makedev: (major: number, minor: number) => number;
    registerDevice: (
      device: number,
      ops: {
        open: (stream: { seekable: boolean }) => void;
        write: (
          stream: unknown,
          buffer: Uint8Array,
          offset: number,
          length: number,
        ) => number;
      },
    ) => void;
    mkdev: (path: string, mode: number, device: number) => void;
  };
}

let core: Core;
let lastError = "";
self.onmessage = async (
  event: MessageEvent<{
    type: "load" | "convert";
    file: File;
    coreURL: string;
    wasmURL: string;
  }>,
) => {
  try {
    if (event.data.type === "load") {
      const { file, coreURL, wasmURL } = event.data;
      const factory = (await import(/* @vite-ignore */ coreURL))
        .default as (options: { mainScriptUrlOrBlob: string }) => Promise<Core>;
      core = await factory({
        mainScriptUrlOrBlob: `${coreURL}#${btoa(JSON.stringify({ wasmURL }))}`,
      });
      core.setLogger(({ message }) => {
        lastError = message;
      });
      core.FS.writeFile("input", new Uint8Array(await file.arrayBuffer()));
      core.setTimeout(-1);
      core.ffprobe(
        "-v",
        "error",
        "-show_entries",
        "format=duration:stream=codec_type,codec_name,profile",
        "-of",
        "json",
        "input",
        "-o",
        "probe.json",
      );
      // Some core builds leave ret at -1 on a normal ffprobe return.
      // Reading and parsing its JSON output also verifies successful probing.
      if (core.ret > 0)
        throw new Error(
          `Could not inspect the media tracks (${core.ret}): ${lastError}`,
        );
      const probe = JSON.parse(
        core.FS.readFile("probe.json", { encoding: "utf8" }),
      ) as MediaProbe;
      core.reset();
      self.postMessage({ type: "probe", probe });
      return;
    }
    let bytes = 0;
    const device = core.FS.makedev(80, 0);
    core.FS.registerDevice(device, {
      open: (stream) => {
        stream.seekable = false;
      },
      write: (_stream, buffer, offset, length) => {
        bytes += length;
        if (bytes > 128 * 1024 * 1024)
          throw new Error(
            "Converted media exceeds the 128 MB playback buffer. Trim the file first.",
          );
        const data = new Uint8Array(length);
        data.set(buffer.subarray(offset, offset + length));
        self.postMessage({ type: "data", data }, { transfer: [data.buffer] });
        return length;
      },
    });
    core.FS.mkdev("/dev/media-output", 0o666, device);
    self.postMessage({ type: "encoding" });
    core.setTimeout(-1);
    core.exec(
      "-nostdin",
      "-y",
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
      "-profile:v",
      "baseline",
      "-level:v",
      "4.2",
      "-bf",
      "0",
      "-force_key_frames",
      "expr:gte(t,n_forced*1)",
      "-c:a",
      "aac",
      "-b:a",
      "128k",
      "-ac",
      "2",
      "-ar",
      "48000",
      "-movflags",
      "+frag_keyframe+empty_moov+default_base_moof",
      "-frag_duration",
      "1000000",
      "-flush_packets",
      "1",
      "-f",
      "mp4",
      "/dev/media-output",
    );
    if (core.ret > 0 || bytes === 0)
      throw new Error(`Could not convert the media: ${lastError}`);
    self.postMessage({ type: "done" });
  } catch (error) {
    self.postMessage({ type: "error", message: String(error) });
  }
};
