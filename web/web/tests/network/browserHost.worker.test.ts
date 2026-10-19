import { afterEach, expect, it, vi } from "vite-plus/test";
const wasm = vi.hoisted(() => ({
  hostPacketCallback: undefined as
    | undefined
    | ((id: number, bytes: Uint8Array) => void),
  _browser_host_create: () => 0,
  _browser_host_add_client: () => 1,
  _browser_host_set_local_client: vi.fn(() => 0),
  _browser_host_render: vi.fn(() => 0),
  _browser_host_render_audio: vi.fn(() => 0),
}));
vi.mock("../../src/wasm/client", () => ({
  initClientWasm: async () => {},
  getClientModule: () => wasm,
  PacketType: {},
}));
vi.mock("../../src/network/BrowserVideoDecoder", () => ({
  BrowserVideoDecoder: class {},
}));
afterEach(() => {
  vi.useRealTimers();
  vi.unstubAllGlobals();
  vi.resetModules();
});
it("pipelines two batches, bounds backlog, and resumes on acknowledgement without polling", async () => {
  vi.useFakeTimers({
    toFake: [
      "setTimeout",
      "clearTimeout",
      "setInterval",
      "clearInterval",
      "performance",
    ],
  });
  const scope = { postMessage: vi.fn(), onmessage: undefined as any };
  vi.stubGlobal("self", scope);
  wasm._browser_host_render.mockImplementation(() => {
    wasm.hostPacketCallback?.(1, new Uint8Array([1]));
    return 0;
  });
  await import("../../src/network/browserHost.worker");
  const send = async (data: object) => {
    scope.onmessage({ data });
    for (let i = 0; i < 8; i++) await Promise.resolve();
  };
  await send({ kind: "init" });
  await send({ kind: "add", id: 1, local: true });
  expect(wasm._browser_host_set_local_client).toHaveBeenCalledWith(1);
  await vi.advanceTimersByTimeAsync(34);
  const batches = () =>
    scope.postMessage.mock.calls.filter(([m]) => m.kind === "packets");
  expect(batches()).toHaveLength(2);
  await vi.advanceTimersByTimeAsync(100);
  expect(batches()).toHaveLength(2);
  expect(vi.getTimerCount()).toBe(1); // Audio continues on its independent clock.
  expect(wasm._browser_host_render_audio).toHaveBeenCalled();
  await send({
    kind: "video",
    id: 1,
    frame: {
      displayWidth: 2,
      displayHeight: 2,
      copyTo: () => new Promise(() => {}),
      close: vi.fn(),
    },
  });
  // Even a stalled GPU readback must not delay the presentation ack.
  await send({ kind: "presented" });
  // A due frame is produced by the ack task itself, without a timer wake-up.
  expect(batches()).toHaveLength(3);
  expect(vi.getTimerCount()).toBe(1);
});
