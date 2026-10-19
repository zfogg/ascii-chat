import { afterEach, describe, expect, it, vi } from "vitest";
import { BrowserHostRuntime } from "../../src/network/BrowserHostRuntime";
import { PacketType } from "../../src/wasm/client";

afterEach(() => vi.unstubAllGlobals());
describe("browser host worker backpressure", () => {
  function setup() {
    const sent: any[] = [];
    let worker: any;
    vi.stubGlobal(
      "Worker",
      class {
        onmessage: any;
        onerror: any;
        constructor() {
          worker = this;
        }
        postMessage(message: unknown) {
          sent.push(message);
        }
        terminate = vi.fn();
      },
    );
    const packet = vi.fn(),
      failure = vi.fn();
    const runtime = new BrowserHostRuntime(packet, failure);
    const ready = runtime.start();
    worker.onmessage({ data: { kind: "ready" } });
    return { runtime, ready, sent, worker, packet, failure };
  }
  it("keeps only the newest waiting raw frame and cancels it on stream control", async () => {
    const { runtime, ready, sent, worker } = setup();
    await ready;
    const id = runtime.addClient();
    for (const value of [1, 2, 3])
      runtime.receive(id, PacketType.IMAGE_FRAME, new Uint8Array([value]));
    expect(sent.filter((m) => m.kind === "receive")).toHaveLength(1);
    worker.onmessage({ data: { kind: "received", id } });
    expect(sent.at(-1).bytes[0]).toBe(3);
    runtime.receive(id, PacketType.IMAGE_FRAME, new Uint8Array([4]));
    runtime.removeClient(id);
    worker.onmessage({ data: { kind: "received", id } });
    expect(sent.at(-1).kind).toBe("remove");
    runtime.close();
    expect(worker.terminate).toHaveBeenCalledOnce();
  });
  it("bounds transferable local frames and releases superseded and stopped frames", async () => {
    const { runtime, ready, sent, worker } = setup();
    await ready;
    const id = runtime.addClient();
    const frames = Array.from(
      { length: 5 },
      () => ({ close: vi.fn() }) as unknown as VideoFrame,
    );
    runtime.receiveVideoFrame(id, frames[0]!);
    runtime.receiveVideoFrame(id, frames[1]!);
    runtime.receiveVideoFrame(id, frames[2]!);
    expect(frames[1]!.close).toHaveBeenCalledOnce();
    expect(sent.filter((m) => m.kind === "video")).toHaveLength(1);
    worker.onmessage({ data: { kind: "received-video", id } });
    expect(sent.at(-1).frame).toBe(frames[2]);
    runtime.receiveVideoFrame(id, frames[3]!);
    runtime.removeClient(id);
    expect(frames[3]!.close).toHaveBeenCalledOnce();
    runtime.close();
    runtime.receiveVideoFrame(id, frames[4]!);
    expect(frames[4]!.close).toHaveBeenCalledOnce();
  });
  it("acknowledges output only after delivery", async () => {
    const { runtime, ready, sent, worker, packet } = setup();
    await ready;
    packet.mockImplementation(() =>
      expect(sent.at(-1).kind).not.toBe("presented"),
    );
    worker.onmessage({
      data: {
        kind: "packets",
        packets: [{ id: 1, bytes: new Uint8Array([7]) }],
      },
    });
    expect(packet).toHaveBeenCalledOnce();
    expect(sent.at(-1).kind).toBe("presented");
    runtime.close();
  });
});
