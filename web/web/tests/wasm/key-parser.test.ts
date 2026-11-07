import { afterEach, expect, it, vi } from "vite-plus/test";
import {
  PacketType,
  packetTypeName,
  validateSshPrivateKey,
} from "../../src/wasm/client";

it("does not publish errors for expected key format probes", async () => {
  const reports = vi.fn();
  window.addEventListener("ascii-chat:error", reports);
  class ParserWorker {
    onmessage: ((event: MessageEvent) => void) | null = null;
    onerror: ((event: ErrorEvent) => void) | null = null;
    postMessage(request: { id: number; kind: string }) {
      const result =
        request.kind === "ssh-private"
          ? {
              id: request.id,
              error: "not an SSH key",
              frames: [{ message: "expected" }],
            }
          : { id: request.id, bytes: new Uint8Array([1, 2]).buffer };
      queueMicrotask(() => this.onmessage?.({ data: result } as MessageEvent));
    }
    terminate() {}
  }
  vi.stubGlobal("Worker", ParserWorker);
  try {
    expect(await validateSshPrivateKey("armored key")).toEqual(
      new Uint8Array([1, 2]),
    );
    expect(reports).not.toHaveBeenCalled();
  } finally {
    window.removeEventListener("ascii-chat:error", reports);
  }
});

afterEach(() => vi.unstubAllGlobals());

it("names H.264 packets", () => {
  expect(packetTypeName(PacketType.IMAGE_FRAME_H264)).toBe("IMAGE_FRAME_H264");
});
