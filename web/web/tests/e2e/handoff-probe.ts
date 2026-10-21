import { BrowserHostSession } from "../../src/network/BrowserHostSession";
import { WebRTCSession } from "../../src/network/WebRTCSession";
import { parseAsciiFrame } from "../../src/network/AsciiFrameParser";
import {
  buildCapabilitiesPacket,
  buildStreamStartPacket,
} from "../../src/network/packetBuilders";
import { ConnectionState, PacketType } from "../../src/wasm/client";
import { AudioPipeline } from "../../src/audio/AudioPipeline";
import type { ClientSession } from "../../src/network/Transport";

let session: ClientSession | null = null;
let audio: AudioPipeline | null = null;
const peers: RTCPeerConnection[] = [];
const OriginalPeer = window.RTCPeerConnection;
window.RTCPeerConnection = class extends OriginalPeer {
  constructor(configuration?: RTCConfiguration) {
    super(configuration);
    peers.push(this);
  }
};
interface ProbeOptions {
  audio?: boolean;
  turnServers?: string[];
  turnUsername?: string;
  turnCredential?: string;
  iceTransportPolicy?: RTCIceTransportPolicy;
}
const probe = {
  audioBlocks: 0,
  audioSamples: 0,
  lastAudioAt: 0,
  audioTimes: [] as number[],
  async routes() {
    const routes = [];
    for (const [peer, connection] of peers.entries()) {
      if (connection.connectionState !== "connected") continue;
      const stats = await connection.getStats();
      for (const report of stats.values()) {
        if (report.type !== "transport" || !report.selectedCandidatePairId)
          continue;
        const pair = stats.get(report.selectedCandidatePairId);
        const local = stats.get(pair.localCandidateId);
        const remote = stats.get(pair.remoteCandidateId);
        routes.push({
          peer,
          state: pair.state,
          local: local.candidateType,
          remote: remote.candidateType,
          bytesSent: pair.bytesSent,
          bytesReceived: pair.bytesReceived,
        });
      }
    }
    return routes;
  },
  name: "",
  participant: "",
  migrations: 0,
  frames: 0,
  unique: 0,
  status: "Idle",
  error: "",
  art: "",
  states: [] as number[],
  async start(
    create: boolean,
    name: string,
    signalingUrl: string,
    config: ProbeOptions = {},
  ) {
    const options = {
      createSession: create,
      sessionName: name,
      password: "",
      signalingUrl,
      stunServers: [] as string[],
      turnServers: [] as string[],
      ...config,
      onCreated: (value: string) => {
        probe.name = value;
      },
      onProgress: (value: string) => {
        if (value === "Migrating to elected successor") probe.migrations++;
        console.info("[HandoffProbe]", value);
        probe.status = value;
        document.querySelector("#status")!.textContent = value;
      },
    };
    session = create
      ? new BrowserHostSession(options, 40, 16)
      : new WebRTCSession(options, 40, 16);
    session.onStateChange((state) => {
      probe.states.push(state);
      console.info("[HandoffProbe] state", state);
      if (state === ConnectionState.CONNECTED) {
        session!.sendPacket(
          PacketType.CLIENT_CAPABILITIES,
          buildCapabilitiesPacket(40, 16, 20),
        );
        session!.sendPacket(
          PacketType.STREAM_START,
          buildStreamStartPacket(Boolean(config.audio)),
        );
      }
    });
    session.onPacketReceived((packet, payload) => {
      if (
        packet.type === PacketType.AUDIO_BATCH ||
        packet.type === PacketType.AUDIO_OPUS_BATCH
      ) {
        try {
          audio?.playPacket(packet.type, payload);
        } catch (error) {
          probe.error = String(error);
        }
        return;
      }
      if (packet.type !== PacketType.ASCII_FRAME) return;
      const frame = parseAsciiFrame(payload);
      const art = frame.ansiString.replace(/\x1b\[[0-?]*[ -/]*[@-~]/g, "");
      probe.frames++;
      if (art !== probe.art) probe.unique++;
      probe.art = art;
      document.querySelector("#media")!.textContent = art;
    });
    await session.connect();
    if (config.audio) {
      audio = new AudioPipeline({
        onAudioData(payload) {
          if (session?.getState() === ConnectionState.CONNECTED)
            session.sendPacket(PacketType.AUDIO_OPUS_BATCH, payload);
        },
        onError(error) {
          probe.error = String(error);
        },
        onPlaybackSamples(samples) {
          const rms = Math.sqrt(
            samples.reduce((sum, n) => sum + n * n, 0) / samples.length,
          );
          if (rms < 0.005) return;
          probe.audioBlocks++;
          probe.audioSamples += samples.length;
          probe.lastAudioAt = performance.now();
          probe.audioTimes.push(probe.lastAudioAt);
          document.querySelector("#audio")!.textContent =
            `Decoded remote audio: ${probe.audioBlocks} blocks, ${probe.audioSamples} samples`;
        },
      });
      await audio.enablePlayback();
      await audio.startCapture();
    }
    const identity = (
      session as unknown as { joined: { participantId: Uint8Array } }
    ).joined.participantId;
    probe.participant = Array.from(identity.slice(0, 4), (n) =>
      n.toString(16).padStart(2, "0"),
    ).join("");
  },
  stop() {
    audio?.close();
    audio = null;
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
