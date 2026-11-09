import { initClientWasm, getClientModule, PacketType } from "../wasm/client";
import { buildImageFramePayload } from "./packetBuilders";
import { BrowserVideoDecoder } from "./BrowserVideoDecoder";
import type { BrowserHostBinding } from "@ascii-chat/shared/wasm";

const post = self.postMessage as unknown as (
  message: unknown,
  transfer?: Transferable[],
) => void;
let module: NonNullable<ReturnType<typeof getClientModule>>;
let host: BrowserHostBinding;
const clients = new Map<number, number>();
const decoders = new Map<number, BrowserVideoDecoder>();
const videoGenerations = new Map<number, number>();
let packets: { id: number; bytes: Uint8Array }[] = [];
let pendingBatches = 0;
let renderTimer: ReturnType<typeof setTimeout> | null = null;
function scheduleRender() {
  if (renderTimer !== null || pendingBatches >= 2) return;
  renderTimer = setTimeout(
    () => {
      renderTimer = null;
      render();
    },
    Math.max(0, nextFrameAt - performance.now()),
  );
}
function renderIfDue() {
  if (!module || pendingBatches >= 2 || performance.now() < nextFrameAt) return;
  if (renderTimer !== null) clearTimeout(renderTimer);
  renderTimer = null;
  render();
}
let nextFrameAt = performance.now();
let started = performance.now(),
  frames = 0,
  totalMs = 0,
  maxMs = 0;

function fail(error: unknown) {
  post({
    kind: "error",
    error: error instanceof Error ? error.message : String(error),
  });
}
let inputTiming = {
  rawMs: 0,
  encodedMs: 0,
  rawCount: 0,
  encodedCount: 0,
  ackMs: 0,
  ackCount: 0,
};
function receive(id: number, type: number, bytes: Uint8Array) {
  const start = performance.now();
  receivePacket(id, type, bytes);
  if (type === PacketType.IMAGE_FRAME) {
    inputTiming.rawMs += performance.now() - start;
    inputTiming.rawCount++;
  } else {
    inputTiming.encodedMs += performance.now() - start;
    inputTiming.encodedCount++;
  }
}
function receivePacket(id: number, type: number, bytes: Uint8Array) {
  const client = clients.get(id);
  if (!client) return;
  if (type === PacketType.STREAM_START || type === PacketType.STREAM_STOP)
    videoGenerations.set(id, (videoGenerations.get(id) ?? 0) + 1);
  if (
    type === PacketType.IMAGE_FRAME_H264 ||
    type === PacketType.IMAGE_FRAME_H265
  ) {
    let decoder = decoders.get(id);
    if (!decoder) {
      decoder = new BrowserVideoDecoder(
        (pixels) => receive(id, PacketType.IMAGE_FRAME, pixels),
        fail,
      );
      decoders.set(id, decoder);
    }
    decoder.receive(type, bytes);
    return;
  }
  if (host.receive(client, type, bytes))
    throw new Error("C host rejected packet.");
}
function render() {
  try {
    if (pendingBatches < 2) {
      const start = performance.now();
      if (host.render())
        throw new Error("C host could not render video.");
      const duration = performance.now() - start;
      totalMs += duration;
      maxMs = Math.max(maxMs, duration);
      frames++;
      if (packets.length) {
        pendingBatches++;
        post(
          { kind: "packets", packets, sentAt: performance.now() },
          packets.map((packet) => packet.bytes.buffer as ArrayBuffer),
        );
        packets = [];
      }
      if (performance.now() - started >= 5000) {
        post({
          kind: "timing",
          fps: (frames * 1000) / (performance.now() - started),
          avgMs: totalMs / frames,
          maxMs,
          input: inputTiming,
        });
        inputTiming = {
          rawMs: 0,
          encodedMs: 0,
          rawCount: 0,
          encodedCount: 0,
          ackMs: 0,
          ackCount: 0,
        };
        started = performance.now();
        frames = 0;
        totalMs = 0;
        maxMs = 0;
      }
      // Keep an absolute cadence. Timer and main-thread delays must not be
      // added to every frame interval, and acknowledgement needs no polling.
      const now = performance.now();
      nextFrameAt += 1000 / 60;
      if (nextFrameAt < now) nextFrameAt = now;
      scheduleRender();
    }
  } catch (error) {
    fail(error);
  }
}
// C emits SERVER_STATE synchronously while attaching the new transport.
// Preserve its logical recipient before the C id is returned and registered.
let admittingClient: number | null = null;
let queue = Promise.resolve();
self.onmessage = (event) => {
  queue = queue
    .then(async () => {
      const message = event.data;
      if (message.kind === "init") {
        await initClientWasm();
        const clientModule = getClientModule();
        if (!clientModule) throw new Error("Client WASM module is unavailable");
        module = clientModule;
        host = new module.BrowserHostBinding();
        if (!host.valid())
          throw new Error("Could not initialize C host worker.");
        module.hostPacketCallback = (actual, bytes) => {
          if (
            pendingBatches >= 2 &&
            new DataView(
              bytes.buffer,
              bytes.byteOffset,
              bytes.byteLength,
            ).getUint16(8) === PacketType.AUDIO_OPUS_BATCH
          )
            return;
          const entry = [...clients].find(([, value]) => value === actual);
          const id = entry?.[0] ?? admittingClient;
          if (id !== null) packets.push({ id, bytes });
        };
        post({ kind: "ready" });
        nextFrameAt = performance.now();
        render();
        setInterval(() => {
          try {
            if (host.renderAudio())
              throw new Error("C host could not mix audio.");
            if (packets.length) {
              pendingBatches++;
              post(
                { kind: "packets", packets, sentAt: performance.now() },
                packets.map((packet) => packet.bytes.buffer as ArrayBuffer),
              );
              packets = [];
            }
          } catch (error) {
            fail(error);
          }
        }, 20);
      } else if (message.kind === "add") {
        let id: number;
        admittingClient = message.id;
        try {
          id = host.addClient();
        } finally {
          admittingClient = null;
        }
        if (!id) throw new Error("Host participant limit reached.");
        clients.set(message.id, id);
        if (message.local && host.setLocalClient(id))
          throw new Error("Could not configure the host's local display.");
      } else if (message.kind === "remove") {
        const id = clients.get(message.id);
        if (id) host.removeClient(id);
        clients.delete(message.id);
        videoGenerations.delete(message.id);
        decoders.get(message.id)?.destroy();
        decoders.delete(message.id);
      } else if (message.kind === "video") {
        const frame: VideoFrame = message.frame;
        const client = clients.get(message.id);
        const generation = videoGenerations.get(message.id) ?? 0;
        const width = frame.displayWidth,
          height = frame.displayHeight;
        const pixels = new Uint8Array(width * height * 4);
        // GPU readback must not block control messages or presentation acks.
        // The runtime already bounds this to one local readback in flight.
        void frame
          .copyTo(pixels, {
            format: "RGBA",
            layout: [{ offset: 0, stride: width * 4 }],
          })
          .then(() => {
            if (
              client &&
              clients.get(message.id) === client &&
              (videoGenerations.get(message.id) ?? 0) === generation
            )
              receive(
                message.id,
                PacketType.IMAGE_FRAME,
                buildImageFramePayload(pixels, width, height),
              );
          })
          .catch(fail)
          .finally(() => {
            frame.close();
            post({ kind: "received-video", id: message.id });
          });
      } else if (message.kind === "receive") {
        receive(message.id, message.type, message.bytes);
        if (message.type === PacketType.IMAGE_FRAME)
          post({ kind: "received", id: message.id });
      } else if (message.kind === "presented" && pendingBatches > 0) {
        if (typeof message.sentAt === "number") {
          inputTiming.ackMs += performance.now() - message.sentAt;
          inputTiming.ackCount++;
        }
        pendingBatches--;
        scheduleRender();
      }
      // Video arrivals can service a due tick even when timers are deprioritized.
      renderIfDue();
    })
    .catch(fail);
};
