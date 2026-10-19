import {
  afterEach,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from "vite-plus/test";
import { AudioCapture } from "../../src/audio/AudioCapture";

let worker: {
  onmessage: ((event: { data: unknown }) => void) | null;
  onerror: ((event: { message: string }) => void) | null;
  postMessage: ReturnType<typeof vi.fn>;
  terminate: ReturnType<typeof vi.fn>;
};
let node: {
  connect: ReturnType<typeof vi.fn>;
  disconnect: ReturnType<typeof vi.fn>;
  port: {
    postMessage: ReturnType<typeof vi.fn>;
    close: ReturnType<typeof vi.fn>;
  };
};
let mode: "ready" | "failure" | "throw";
const options = () => ({ bypass: false, onFrame: vi.fn(), onError: vi.fn() });
const context = () =>
  ({
    sampleRate: 48000,
    destination: {},
    audioWorklet: { addModule: vi.fn().mockResolvedValue(undefined) },
  }) as unknown as AudioContext;

beforeEach(() => {
  mode = "ready";
  node = {
    connect: vi.fn(),
    disconnect: vi.fn(),
    port: { postMessage: vi.fn(), close: vi.fn() },
  };
  vi.stubGlobal(
    "AudioWorkletNode",
    class {
      constructor() {
        return node;
      }
    },
  );
  vi.stubGlobal(
    "Worker",
    class {
      constructor() {
        if (mode === "throw") throw new Error("worker construction failed");
        worker = {
          onmessage: null,
          onerror: null,
          terminate: vi.fn(),
          postMessage: vi.fn(() =>
            queueMicrotask(() =>
              worker.onmessage?.({
                data:
                  mode === "failure"
                    ? { error: "DSP initialization failed" }
                    : { ready: true },
              }),
            ),
          ),
        };
        return worker;
      }
    },
  );
  vi.stubGlobal(
    "MessageChannel",
    class {
      port1 = {};
      port2 = {};
    },
  );
});
afterEach(() => vi.unstubAllGlobals());

describe("AudioCapture worker lifecycle", () => {
  it("waits for DSP readiness, forwards frames and tears down resources", async () => {
    const callbacks = options();
    const capture = await AudioCapture.create(context(), callbacks);
    const payload = new Uint8Array([1, 2]);
    const samples = new Float32Array([0.25]);
    worker.onmessage!({ data: { payload, samples } });
    expect(callbacks.onFrame).toHaveBeenCalledWith(payload, samples);
    capture.close();
    expect(worker.terminate).toHaveBeenCalledOnce();
    expect(node.disconnect).toHaveBeenCalledOnce();
    expect(node.port.close).toHaveBeenCalledOnce();
    expect(worker.onmessage).toBeNull();
  });
  it("releases nodes when the worker cannot be constructed", async () => {
    mode = "throw";
    await expect(AudioCapture.create(context(), options())).rejects.toThrow(
      "worker construction failed",
    );
    expect(node.disconnect).toHaveBeenCalledOnce();
    expect(node.port.close).toHaveBeenCalledOnce();
  });
  it("releases the worker and node after a DSP startup failure", async () => {
    mode = "failure";
    await expect(AudioCapture.create(context(), options())).rejects.toThrow(
      "DSP initialization failed",
    );
    expect(worker.terminate).toHaveBeenCalledOnce();
    expect(node.disconnect).toHaveBeenCalledOnce();
  });
  it("reports processing failures to the microphone owner", async () => {
    const callbacks = options();
    const capture = await AudioCapture.create(context(), callbacks);
    worker.onmessage!({ data: { error: "DSP failed" } });
    expect(callbacks.onError).toHaveBeenCalledWith(
      expect.objectContaining({ message: "DSP failed" }),
    );
    capture.close();
  });
});
