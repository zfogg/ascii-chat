import { BrowserVideoDecoder } from "./BrowserVideoDecoder";
import { ClientConnection } from "./ClientConnection";
import { WebRTCBridge } from "./WebRTCBridge";
import type { ClientSession } from "./Transport";
import {
  discoveryRTCConfiguration,
  type DiscoveryOptions,
} from "./WebRTCSession";
import {
  AcdsType,
  joinRequest,
  parseJoined,
  parseSignal,
  readString,
  signalPacket,
  type JoinedSession,
} from "./acdsProtocol";
import {
  ConnectionState,
  PacketType,
  getClientModule,
  parsePacket,
  type ParsedPacket,
} from "../wasm/client";

type HostModule = NonNullable<ReturnType<typeof getClientModule>> & {
  _browser_session_create_request(password: number, output: number): number;
  _browser_host_create(): number;
  _browser_host_destroy(): void;
  _browser_host_add_client(): number;
  _browser_host_remove_client(id: number): void;
  _browser_host_receive(
    id: number,
    type: number,
    data: number,
    length: number,
  ): number;
  _browser_host_render(): number;
  hostPacketCallback?: (id: number, packet: Uint8Array) => void;
};
type Peer = {
  pc: RTCPeerConnection;
  bridge: WebRTCBridge | null;
  id: number;
  session: JoinedSession;
  ice: RTCIceCandidateInit[];
};

/** Browser scheduling and transport adapter; media hosting stays in session/host.c. */
export class BrowserHostSession implements ClientSession {
  readonly transportType = "webrtc" as const;
  private decoders = new Map<number, BrowserVideoDecoder>();
  private signaling: ClientConnection;
  private module: HostModule | null = null;
  private joined: JoinedSession | null = null;
  private peers = new Map<string, Peer>();
  private localId = 0;
  private state = ConnectionState.DISCONNECTED;
  private stateCallback: ((state: ConnectionState) => void) | null = null;
  private packetCallback:
    | ((packet: ParsedPacket, payload: Uint8Array) => void)
    | null = null;
  private closed = false;
  private timer: ReturnType<typeof setInterval> | null = null;
  private deadline: ReturnType<typeof setTimeout> | null = null;
  private queue = Promise.resolve();
  private resolve: (() => void) | null = null;
  private reject: ((error: Error) => void) | null = null;
  private name = "";
  constructor(
    private options: DiscoveryOptions,
    width = 80,
    height = 40,
  ) {
    this.signaling = new ClientConnection({
      serverUrl: options.signalingUrl,
      discoveryHandshake: true,
      ...(options.applicationEncryption === undefined
        ? {}
        : { applicationEncryption: options.applicationEncryption }),
      ...(options.cryptoOptions
        ? { cryptoOptions: options.cryptoOptions }
        : {}),
      width,
      height,
    });
  }
  async connect(): Promise<void> {
    this.setState(ConnectionState.CONNECTING);
    const ready = new Promise<void>((resolve, reject) => {
      this.resolve = resolve;
      this.reject = reject;
    });
    void ready.catch(() => {});
    this.deadline = setTimeout(
      () => this.fail(new Error("Session creation timed out.")),
      30000,
    );
    let requested = false;
    this.signaling.onStateChange((state) => {
      if (this.closed) return;
      if (state === ConnectionState.CONNECTED && !requested) {
        requested = true;
        try {
          this.module = getClientModule() as HostModule;
          const password = new TextEncoder().encode(this.options.password);
          if (password.length > 127)
            throw new Error("Password must be at most 127 UTF-8 bytes.");
          const ptr = this.module._malloc(password.length + 1);
          const output = this.module._malloc(306);
          try {
            this.module.stringToUTF8(
              this.options.password,
              ptr,
              password.length + 1,
            );
            if (this.module._browser_session_create_request(ptr, output))
              throw new Error("Could not prepare session creation.");
            this.signaling.sendPacket(
              6000,
              this.module.HEAPU8.slice(output, output + 306),
            );
          } finally {
            this.module._free(ptr);
            this.module._free(output);
          }
        } catch (error) {
          this.fail(error);
        }
      } else if (
        state === ConnectionState.ERROR ||
        state === ConnectionState.DISCONNECTED
      ) {
        this.fail(new Error("Discovery connection closed."));
      }
    });
    this.signaling.onPacketReceived((packet, payload) => {
      this.queue = this.queue
        .then(() =>
          this.closed ? undefined : this.signal(packet.type, payload),
        )
        .catch((error) => this.fail(error));
    });
    try {
      await this.signaling.connect();
    } catch (error) {
      this.fail(error);
    }
    return ready;
  }
  private async signal(type: number, payload: Uint8Array): Promise<void> {
    if (type === 6001) {
      if (payload.length < 91) throw new Error("Truncated created session.");
      this.name = readString(payload, 1, 48);
      this.signaling.sendPacket(
        AcdsType.JOIN,
        joinRequest(this.name, this.options.password),
      );
    } else if (type === AcdsType.JOINED) {
      this.joined = parseJoined(payload);
      if (
        !this.joined.participantId.every((v, i) => v === this.joined!.hostId[i])
      )
        throw new Error("Creator was not assigned the host role.");
      const module = this.module!;
      if (module._browser_host_create())
        throw new Error("Could not initialize C session host.");
      module.hostPacketCallback = (id, bytes) => {
        // Deliver after C releases its host/client locks.
        queueMicrotask(() => {
          if (this.closed) return;
          if (id === this.localId) {
            const packet = parsePacket(bytes);
            this.packetCallback?.(packet, bytes.slice(22));
          } else {
            for (const peer of this.peers.values())
              if (peer.id === id)
                peer.bridge?.send(
                  bytes,
                  new DataView(bytes.buffer, bytes.byteOffset).getUint16(
                    8,
                    false,
                  ) === PacketType.ASCII_FRAME,
                );
          }
        });
      };
      this.localId = module._browser_host_add_client();
      if (!this.localId)
        throw new Error("Could not add local host participant.");
      if (this.deadline) clearTimeout(this.deadline);
      this.options.onCreated?.(this.name);
      this.setState(ConnectionState.CONNECTED);
      this.options.onProgress?.("Hosting — waiting for a participant");
      this.timer = setInterval(() => {
        if (this.closed) return;
        try {
          if (module._browser_host_render())
            throw new Error("C host could not render video.");
        } catch (error) {
          this.fail(error);
        }
      }, 33);
      this.resolve?.();
      this.resolve = null;
      this.reject = null;
    } else if (
      (type === AcdsType.SDP || type === AcdsType.ICE) &&
      this.joined
    ) {
      if (
        payload.length < 50 ||
        !this.joined.sessionId.every((v, i) => payload[i] === v)
      )
        return;
      if (
        payload.subarray(32, 48).some(Boolean) &&
        !this.joined.participantId.every((v, i) => payload[32 + i] === v)
      )
        return;
      const sender = payload.slice(16, 32);
      if (sender.every((v, i) => this.joined!.participantId[i] === v)) return;
      const key = Array.from(sender).join(",");
      let peer = this.peers.get(key);
      if (!peer) {
        const session = { ...this.joined, hostId: sender };
        const pc = new RTCPeerConnection(
          discoveryRTCConfiguration(this.options, this.joined),
        );
        peer = { pc, bridge: null, id: 0, session, ice: [] };
        this.peers.set(key, peer);
        const remote = peer;
        pc.onicecandidate = (event) => {
          if (event.candidate && !this.closed)
            this.signaling.sendPacket(
              AcdsType.ICE,
              signalPacket(
                session,
                "ice",
                event.candidate.candidate,
                event.candidate.sdpMid || "0",
              ),
            );
        };
        const channel = pc.createDataChannel("acip", {
          negotiated: true,
          id: 0,
          ordered: true,
        });
        channel.onopen = () => {
          if (this.closed) return;
          remote.bridge = new WebRTCBridge(
            channel,
            (bytes) => {
              try {
                const packet = parsePacket(bytes);
                this.receive(remote.id, packet.type, bytes.slice(22));
              } catch (error) {
                this.fail(error);
              }
            },
            (error) => this.fail(error),
            pc.sctp?.maxMessageSize,
          );
          remote.id = this.module!._browser_host_add_client();
          if (!remote.id) {
            channel.close();
            return;
          }
          this.options.onProgress?.(
            "Hosting — participant connected (DTLS encrypted)",
          );
        };
        channel.onclose = () => {
          if (this.closed) return;
          if (remote.id) this.module?._browser_host_remove_client(remote.id);
          this.decoders.get(remote.id)?.destroy();
          this.decoders.delete(remote.id);
          this.peers.delete(key);
        };
      }
      if (type === AcdsType.SDP) {
        const description = parseSignal(
          payload,
          peer.session,
          false,
        ) as RTCSessionDescriptionInit;
        if (description.type !== "offer")
          throw new Error("Expected a participant offer.");
        await peer.pc.setRemoteDescription(description);
        for (const candidate of peer.ice.splice(0))
          await peer.pc.addIceCandidate(candidate);
        const answer = await peer.pc.createAnswer();
        await peer.pc.setLocalDescription(answer);
        if (!this.closed)
          this.signaling.sendPacket(
            AcdsType.SDP,
            signalPacket(peer.session, "answer", answer.sdp!),
          );
      } else {
        const ice = parseSignal(
          payload,
          peer.session,
          true,
        ) as RTCIceCandidateInit;
        if (peer.pc.remoteDescription) await peer.pc.addIceCandidate(ice);
        else peer.ice.push(ice);
      }
    } else if (type === AcdsType.ERROR || type === AcdsType.END)
      throw new Error(readString(payload, 2, 256) || "Session ended.");
  }
  private receive(id: number, type: number, payload: Uint8Array) {
    if (this.closed) return;
    if (
      type === PacketType.IMAGE_FRAME_H264 ||
      type === PacketType.IMAGE_FRAME_H265
    ) {
      let decoder = this.decoders.get(id);
      if (!decoder) {
        decoder = new BrowserVideoDecoder(
          (pixels) => this.receive(id, PacketType.IMAGE_FRAME, pixels),
          (error) => this.fail(error),
        );
        this.decoders.set(id, decoder);
      }
      decoder.receive(type, payload);
      return;
    }
    const module = this.module!;
    const ptr = module._malloc(payload.length || 1);
    try {
      module.HEAPU8.set(payload, ptr);
      if (module._browser_host_receive(id, type, ptr, payload.length))
        throw new Error("C host rejected packet.");
    } finally {
      module._free(ptr);
    }
  }
  sendPacket(type: number, payload: Uint8Array) {
    this.receive(this.localId, type, payload);
  }
  sendUnencryptedAcipPacket(type: number, payload: Uint8Array) {
    this.sendPacket(type, payload);
  }
  getBufferedAmount() {
    return Math.max(
      0,
      ...Array.from(
        this.peers.values(),
        (peer) => peer.bridge?.getBufferedAmount() || 0,
      ),
    );
  }
  onStateChange(callback: (state: ConnectionState) => void) {
    this.stateCallback = callback;
  }
  onPacketReceived(
    callback: (packet: ParsedPacket, payload: Uint8Array) => void,
  ) {
    this.packetCallback = callback;
  }
  getState() {
    return this.state;
  }
  getPublicKey() {
    return null;
  }
  private setState(state: ConnectionState) {
    this.state = state;
    this.stateCallback?.(state);
  }
  private fail(error: unknown) {
    if (this.closed) return;
    this.reject?.(error instanceof Error ? error : new Error(String(error)));
    this.reject = null;
    this.disconnect();
    this.setState(ConnectionState.ERROR);
  }
  disconnect() {
    if (this.closed) return;
    this.closed = true;
    if (this.deadline) clearTimeout(this.deadline);
    if (this.timer) clearInterval(this.timer);
    this.reject?.(new Error("Session creation cancelled."));
    this.reject = null;
    if (this.joined) {
      const leave = new Uint8Array(32);
      leave.set(this.joined.sessionId);
      leave.set(this.joined.participantId, 16);
      try {
        this.signaling.sendPacket(AcdsType.LEAVE, leave);
      } catch {
        /* Already disconnected. */
      }
    }
    for (const peer of this.peers.values()) {
      peer.pc.close();
      peer.bridge?.close();
    }
    this.peers.clear();
    for (const decoder of this.decoders.values()) decoder.destroy();
    this.decoders.clear();
    if (this.module) {
      delete this.module.hostPacketCallback;
      this.module._browser_host_destroy();
    }
    this.signaling.disconnect();
    this.setState(ConnectionState.DISCONNECTED);
  }
}
