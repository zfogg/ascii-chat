import { act, renderHook, waitFor } from "@testing-library/react";
import { beforeEach, afterEach, expect, it, vi } from "vite-plus/test";
import type { AsciiRendererHandle } from "@ascii-chat/shared/components/AsciiRenderer";
import type { AudioPipeline } from "../../src/audio/AudioPipeline";
import { useSessionRecording } from "../../src/hooks/useSessionRecording";

const fixture = vi.hoisted(() => ({
  supported: true,
  start: vi.fn(),
  stop: vi.fn(),
  dispose: vi.fn(),
  complete: (_result: unknown) => {},
}));
vi.mock("../../src/recording/LocalRecording", () => ({
  recordingMimeType: () => (fixture.supported ? "video/webm" : null),
  LocalRecording: class {
    promise: Promise<unknown>;
    constructor(private state: (value: string) => void) {
      this.promise = new Promise((resolve) => {
        fixture.complete = resolve;
      });
    }
    start() {
      fixture.start();
      this.state("recording");
    }
    stop() {
      fixture.stop();
      this.state("finalizing");
      return this.promise;
    }
    completed() {
      return this.promise;
    }
    dispose() {
      fixture.dispose();
      fixture.complete(null);
    }
  },
}));
let pipeline: {
  startRecording: ReturnType<typeof vi.fn>;
  stopRecording: ReturnType<typeof vi.fn>;
  setRecordingMedia: ReturnType<typeof vi.fn>;
};
let ensureAudio: ReturnType<typeof vi.fn>;
beforeEach(() => {
  fixture.supported = true;
  fixture.start.mockClear();
  fixture.stop.mockClear();
  fixture.dispose.mockClear();
  pipeline = {
    startRecording: vi.fn().mockResolvedValue({}),
    stopRecording: vi.fn(),
    setRecordingMedia: vi.fn(),
  };
  ensureAudio = vi.fn(() => pipeline as unknown as AudioPipeline);
  vi.stubGlobal("URL", {
    createObjectURL: vi.fn(() => "blob:download"),
    revokeObjectURL: vi.fn(),
  });
});
afterEach(() => vi.unstubAllGlobals());
const options = () => ({
  rendererRef: {
    current: {
      getCanvas: () => ({ captureStream: vi.fn() }),
    } as unknown as AsciiRendererHandle,
  },
  ensureAudio,
  fileElement: () => null,
  sourceRevision: "camera",
  enabled: true,
});

it("waits for the replacement raster before starting the encoder", async () => {
  const configured = options();
  const canvas = configured.rendererRef.current.getCanvas();
  let ready = false;
  configured.rendererRef.current.getCanvas = () => (ready ? canvas : null);
  const { result } = renderHook(() => useSessionRecording(configured));
  act(() => {
    void result.current.start();
  });
  await waitFor(() => expect(pipeline.startRecording).toHaveBeenCalledOnce());
  expect(fixture.start).not.toHaveBeenCalled();
  expect(result.current.error).toBe("");
  ready = true;
  await waitFor(() => expect(fixture.start).toHaveBeenCalledOnce());
  await act(async () => {
    fixture.complete(null);
  });
});

it("keeps one encoder through repeated starts, waits for final data, and revokes downloads on unmount", async () => {
  const { result, unmount } = renderHook(() => useSessionRecording(options()));
  act(() => {
    void result.current.start();
    void result.current.start();
  });
  await waitFor(() => expect(result.current.state).toBe("recording"));
  expect(fixture.start).toHaveBeenCalledOnce();
  act(() => {
    void result.current.stop();
  });
  expect(result.current.download).toBeNull();
  await act(async () => {
    fixture.complete({ blob: new Blob(["last frame"]), extension: "webm" });
  });
  expect(result.current.download?.url).toBe("blob:download");
  expect(pipeline.stopRecording).toHaveBeenCalledOnce();
  unmount();
  expect(URL.revokeObjectURL).toHaveBeenCalledWith("blob:download");
});

it("cancels delayed audio startup without a late encoder or leaked recording bus", async () => {
  let resolve!: (value: object) => void;
  pipeline.startRecording.mockReturnValue(
    new Promise((done) => {
      resolve = done;
    }),
  );
  const { result } = renderHook(() => useSessionRecording(options()));
  act(() => {
    void result.current.start();
  });
  await waitFor(() => expect(pipeline.startRecording).toHaveBeenCalledOnce());
  act(() => {
    void result.current.stop();
  });
  await act(async () => {
    resolve({});
  });
  expect(fixture.start).not.toHaveBeenCalled();
  expect(pipeline.stopRecording).toHaveBeenCalled();
  expect(result.current.state).toBe("idle");
});

it("unmounts during finalization without creating a download and preserves source media ownership", async () => {
  const { result, unmount } = renderHook(() => useSessionRecording(options()));
  act(() => {
    void result.current.start();
  });
  await waitFor(() => expect(result.current.state).toBe("recording"));
  act(() => {
    void result.current.stop();
  });
  unmount();
  expect(fixture.dispose).toHaveBeenCalledOnce();
  expect(pipeline.stopRecording).toHaveBeenCalledOnce();
  expect(URL.createObjectURL).not.toHaveBeenCalled();
});

it("stops on disconnect and changes file membership without replacing the encoder", async () => {
  const base = options();
  const file = document.createElement("video");
  const { result, rerender } = renderHook(
    ({ revision, enabled }) =>
      useSessionRecording({
        ...base,
        sourceRevision: revision,
        enabled,
        fileElement: () => (revision === "file" ? file : null),
      }),
    { initialProps: { revision: "camera", enabled: true } },
  );
  act(() => {
    void result.current.start();
  });
  await waitFor(() => expect(result.current.state).toBe("recording"));
  rerender({ revision: "file", enabled: true });
  await waitFor(() =>
    expect(pipeline.setRecordingMedia).toHaveBeenLastCalledWith(file),
  );
  rerender({ revision: "file", enabled: false });
  expect(fixture.stop).toHaveBeenCalledOnce();
  expect(fixture.start).toHaveBeenCalledOnce();
});

it("rejects unsupported recording before acquiring an audio graph", async () => {
  fixture.supported = false;
  const { result } = renderHook(() => useSessionRecording(options()));
  await act(async () => {
    await result.current.start();
  });
  expect(result.current.error).toContain("cannot record");
  expect(ensureAudio).not.toHaveBeenCalled();
});

it("reports a source-switch failure consistently and finalizes the recording", async () => {
  const base = options();
  const media = document.createElement("video");
  pipeline.setRecordingMedia.mockImplementation((element) => {
    if (element) throw new Error("File audio is unavailable");
  });
  const { result, rerender } = renderHook(
    ({ file }) =>
      useSessionRecording({
        ...base,
        sourceRevision: file,
        fileElement: () => (file ? media : null),
      }),
    { initialProps: { file: false } },
  );
  act(() => {
    void result.current.start();
  });
  await waitFor(() => expect(result.current.state).toBe("recording"));
  rerender({ file: true });
  await waitFor(() =>
    expect(result.current.error).toBe("File audio is unavailable"),
  );
  expect(fixture.stop).toHaveBeenCalledOnce();
});
