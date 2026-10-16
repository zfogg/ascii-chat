import { beforeEach, expect, test, vi } from "vite-plus/test";
import { transcodeMedia } from "../../src/utils/transcodeMedia";
const backend = vi.hoisted(() => ({
  load: vi.fn(),
  writeFile: vi.fn(),
  ffprobe: vi.fn(),
  exec: vi.fn(),
  readFile: vi.fn(),
  terminate: vi.fn(),
}));
vi.mock("@ffmpeg/ffmpeg", () => ({
  FFmpeg: class {
    constructor() {
      return backend;
    }
  },
}));
vi.mock("@ffmpeg/core?url", () => ({ default: "core.js" }));
vi.mock("@ffmpeg/core/wasm?url", () => ({ default: "core.wasm" }));
beforeEach(() => {
  vi.clearAllMocks();
  backend.ffprobe.mockResolvedValue(0);
  backend.exec.mockResolvedValue(0);
});
async function prepare(codec: string, profile?: string) {
  backend.readFile.mockImplementation(async (name: string) =>
    name === "probe.json"
      ? JSON.stringify({ streams: [{ codec_name: codec, profile }] })
      : new Uint8Array([1]),
  );
  const file = { size: 1, arrayBuffer: async () => new ArrayBuffer(1) } as File;
  const result = await transcodeMedia(
    file,
    new AbortController().signal,
    vi.fn(),
    true,
  );
  return { file, result };
}
test("MP4 with unsupported audio is converted even when video could play", async () => {
  const { file, result } = await prepare("ac3");
  expect(result).not.toBe(file);
  expect(backend.exec).toHaveBeenCalledOnce();
  expect(backend.exec.mock.calls[0]![0]).toContain("0:a:0?");
  expect(backend.terminate).toHaveBeenCalledOnce();
});
test("AAC LC stays native only when the browser confirms codec support", async () => {
  const spy = vi
    .spyOn(HTMLMediaElement.prototype, "canPlayType")
    .mockReturnValue("probably");
  const { file, result } = await prepare("aac", "LC");
  expect(result).toBe(file);
  expect(backend.exec).not.toHaveBeenCalled();
  spy.mockRestore();
});
test("unknown AAC profiles are converted", async () => {
  await prepare("aac", "HE-AACv2");
  expect(backend.exec).toHaveBeenCalledOnce();
});
