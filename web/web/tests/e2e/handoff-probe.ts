import { BrowserHostSession } from "../../src/network/BrowserHostSession";
import { WebRTCSession } from "../../src/network/WebRTCSession";
import { parseAsciiFrame } from "../../src/network/AsciiFrameParser";
import {
  buildCapabilitiesPacket,
  buildStreamStartPacket,
} from "../../src/network/packetBuilders";
import { ConnectionState, PacketType } from "../../src/wasm/client";
import type { ClientSession } from "../../src/network/Transport";

let session: ClientSession | null = null;
const probe = {
  name: "",
  participant: "",
  migrations: 0,
  frames: 0,
  unique: 0,
  status: "Idle",
  error: "",
  art: "",
  states: [] as number[],
  async start(create: boolean, name: string, signalingUrl: string) {
    const options = {
      createSession: create,
      sessionName: name,
      password: "",
      signalingUrl,
      stunServers: [] as string[],
      turnServers: [] as string[],
      onCreated: (value: string) => {
        probe.name = value;
      },
      onProgress: (value: string) => {
        if (value === "Migrating to elected successor") probe.migrations++;
        probe.status = value;
        document.querySelector("#status")!.textContent = value;
      },
    };
    session = create
      ? new BrowserHostSession(options, 40, 16)
      : new WebRTCSession(options, 40, 16);
    session.onStateChange((state) => {
      probe.states.push(state);
      if (state === ConnectionState.CONNECTED) {
        session!.sendPacket(
          PacketType.CLIENT_CAPABILITIES,
          buildCapabilitiesPacket(40, 16, 20),
        );
        session!.sendPacket(PacketType.STREAM_START, buildStreamStartPacket());
      }
    });
    session.onPacketReceived((packet, payload) => {
      if (packet.type !== PacketType.ASCII_FRAME) return;
      const frame = parseAsciiFrame(payload);
      const art = frame.ansiString.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
      probe.frames++;
      if (art !== probe.art) probe.unique++;
      probe.art = art;
      document.querySelector("#media")!.textContent = art;
    });
    await session.connect();
    const identity = (
      session as unknown as { joined: { participantId: Uint8Array } }
    ).joined.participantId;
    probe.participant = Array.from(identity.slice(0, 4), (n) =>
      n.toString(16).padStart(2, "0"),
    ).join("");
  },
  stop() {
    session?.disconnect();
    session = null;
  },
};
let frame = 0;
setInterval(() => {
  if (!session || session.getState() !== ConnectionState.CONNECTED) return;
  const width = 80,
    height = 48;
  const bytes = new Uint8Array(24 + width * height * 3),
    view = new DataView(bytes.buffer);
  view.setUint32(0, width);
  view.setUint32(4, height);
  view.setUint32(8, 1);
  frame++;
  for (let y = 0; y < height; ++y)
    for (let x = 0; x < width; ++x) {
      const offset = 24 + (y * width + x) * 3;
      const value = Math.floor(
        (Math.sin((x + frame * 3) / 7) + Math.cos((y + frame) / 5) + 2) * 63,
      );
      bytes[offset] = value;
      bytes[offset + 1] = value;
      bytes[offset + 2] = value;
    }
  try {
    session.sendPacket(PacketType.IMAGE_FRAME, bytes);
  } catch (error) {
    probe.error = String(error);
  }
}, 50);

Object.assign(window, { handoffProbe: probe });
