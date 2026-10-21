import { OutboundQueue } from "./OutboundQueue";
import { BrowserHostSession } from "./BrowserHostSession";
import { HandoffProtocol } from "./HandoffProtocol";
import { DiscoveryProtocol } from "./DiscoveryProtocol";
import { ClientConnection } from "./ClientConnection";
import { WebRTCBridge } from "./WebRTCBridge";
import type { ClientSession } from "./Transport";
import {
  AcdsType,
  SessionRejectedError,
  parseSignal,
  signalPacket,
  type JoinedSession,
} from "./acdsProtocol";
import {
  PeerCryptoSession,
  ConnectionState,
  PacketType,
  parsePacket,
  serializePacket,
  type ParsedPacket,
} from "../wasm/client";
import type { ClientCryptoOptions } from "../wasm/client";

export interface DiscoveryOptions {
  createSession?: boolean;
  onCreated?: (name: string) => void;
  sessionName: string;
  password: string;
  signalingUrl: string;
  signalingApplicationEncryption?: boolean;
  signalingCryptoOptions?: ClientCryptoOptions;
  applicationEncryption?: boolean;
  cryptoOptions?: ClientCryptoOptions;
  stunServers: string[];
  turnServers: string[];
  iceTransportPolicy?: RTCIceTransportPolicy;
  turnUsername?: string;
  turnCredential?: string;
  onProgress?: (stage: string) => void;
}

export function discoveryRTCConfiguration(
  options: DiscoveryOptions,
  joined: Pick<JoinedSession, "turnUsername" | "turnPassword">,
): RTCConfiguration {
  if (!!options.turnUsername !== !!options.turnCredential)
    throw new Error(
      "Provide both TURN username and password, or leave both blank.",
    );
  const stunServers = options.stunServers.map((url) => {
    if (!/^stun:|^stuns:/i.test(url))
      throw new Error(`STUN URL must start with stun: or stuns:: ${url}`);
    return { urls: url };
  });
  const turnServers = options.turnServers.map((url) => {
    if (!/^turn:|^turns:/i.test(url))
      throw new Error(`TURN URL must start with turn: or turns:: ${url}`);
    const username = options.turnUsername || joined.turnUsername;
    const credential = options.turnCredential || joined.turnPassword;
    if (!username || !credential)
      throw new Error(`TURN server requires credentials: ${url}`);
    return { urls: url, username, credential };
  });
  const iceServers: RTCIceServer[] = [...stunServers, ...turnServers];
  if (options.iceTransportPolicy === "relay" && turnServers.length === 0)
    throw new Error(
      "Relay only requires a TURN server and credentials. Configure them or choose Automatic.",
    );
  return {
    iceServers,
    iceTransportPolicy: options.iceTransportPolicy || "all",
  };
}

/** Signaling stays on ACDS; all media travels on the peer's DTLS DataChannel. */
export class WebRTCSession implements ClientSession {
  canReconnect = true;
  readonly transportType = "webrtc" as const;
  private discovery: DiscoveryProtocol | null = null;
  private handoff: HandoffProtocol | null = null;
  private promoted: BrowserHostSession | null = null;
  private signaling: ClientConnection;
  private peer: RTCPeerConnection | null = null;
  private bridge: WebRTCBridge | null = null;
  private peerCrypto: PeerCryptoSession | null = null;
  private joined: JoinedSession | null = null;
  private state = ConnectionState.DISCONNECTED;
  private stateCallback: ((state: ConnectionState) => void) | null = null;
  private packetCallback:
    | ((packet: ParsedPacket, payload: Uint8Array) => void)
    | null = null;
  private rejectConnect: ((error: Error) => void) | null = null;
  private resolveConnect: (() => void) | null = null;
  private timer: ReturnType<typeof setTimeout> | null = null;
  private disconnectedTimer: ReturnType<typeof setTimeout> | null = null;
  private pendingIce: RTCIceCandidateInit[] = [];
  private signalingQueue = Promise.resolve();
  private closed = false;
  private lookedUp = false;

  /**
   * A native peer installs its DataChannel receive callback while registering
   * the client, after the browser's `open` event can already have fired.  The
   * initial SERVER_STATE packet is sent after that registration, so it is the
   * protocol-ready acknowledgement for browser control traffic.
   */
  private completeConnection(): void {
    if (this.closed || this.state === ConnectionState.CONNECTED) return;
    if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
    this.disconnectedTimer = null;
    this.clearTimer();
    this.setState(ConnectionState.CONNECTED);
    this.options.onProgress?.("Connected over WebRTC (DTLS encrypted)");
    this.resolveConnect?.();
    this.resolveConnect = null;
    this.rejectConnect = null;
  }

  constructor(
    private options: DiscoveryOptions,
    width = 80,
    height = 40,
  ) {
    this.signaling = new ClientConnection({
      serverUrl: options.signalingUrl,
      ...(options.signalingApplicationEncryption === undefined
        ? {}
        : { applicationEncryption: options.signalingApplicationEncryption }),
      ...(options.signalingCryptoOptions
        ? { cryptoOptions: options.signalingCryptoOptions }
        : {}),
      // Plain ws:// signaling uses the ACIP crypto handshake; wss:// relies on
      // TLS when ACDS does not require client identity authentication.
      discoveryHandshake: true,
      width,
      height,
      wasmOptions: {
        stunServers: options.stunServers,
        turnServers: options.turnServers,
        turnUsername: options.turnUsername || "",
        turnCredential: options.turnCredential || "",
      },
    });
  }

  async connect(): Promise<void> {
    if (!!this.options.turnUsername !== !!this.options.turnCredential)
      throw new Error(
        "Provide both TURN username and password, or leave both blank.",
      );
    this.setState(ConnectionState.CONNECTING);
    this.options.onProgress?.("Connecting to discovery service");
    const connected = new Promise<void>((resolve, reject) => {
      this.resolveConnect = resolve;
      this.rejectConnect = reject;
      this.timer = setTimeout(
        () =>
          this.fail(
            new Error(
              "Connection timed out. Check the session name and native host.",
            ),
          ),
        30000,
      );
    });
    // Attach the rejection handler before asynchronous initialization can fail.
    void connected.catch(() => {});
    this.signaling.onStateChange((state) => {
      if (this.closed) return;
      if (state === ConnectionState.CONNECTED && !this.lookedUp) {
        this.lookedUp = true;
        this.options.onProgress?.("Looking up session");
        try {
          this.discovery?.close();
          this.discovery = new DiscoveryProtocol((type, payload) =>
            this.signaling.sendPacket(type, payload),
          );
          this.discovery.enter(
            false,
            this.options.sessionName,
            this.options.password,
          );
        } catch (error) {
          this.fail(error);
        }
      } else if (
        state === ConnectionState.CONNECTING &&
        this.lookedUp &&
        !this.joined
      ) {
        // SocketBridge retries transient signaling drops itself. Let that
        // socket finish its handshake, then look up the session again if the
        // interrupted connection had not joined yet.
        this.lookedUp = false;
        this.options.onProgress?.("Reconnecting to discovery service");
      } else if (
        this.lookedUp &&
        !this.joined &&
        state !== ConnectionState.CONNECTED &&
        state !== ConnectionState.HANDSHAKE &&
        state !== ConnectionState.CONNECTING
      ) {
        this.fail(
          new Error(
            "Discovery service disconnected. Reconnect to rejoin the session.",
          ),
        );
      }
    });
    this.signaling.onPacketReceived((packet, payload) => {
      this.signalingQueue = this.signalingQueue
        .then(async () => {
          if (!this.closed) await this.handleSignaling(packet.type, payload);
        })
        .catch((error: unknown) => this.fail(error));
    });
    try {
      await this.signaling.connect();
    } catch (error) {
      this.fail(error);
    }
    return connected;
  }

  private async handleSignaling(
    type: number,
    payload: Uint8Array,
  ): Promise<void> {
    if (this.handoff?.receive(type, payload)) return;
    const snapshot = this.discovery?.receive(type, payload);
    if (snapshot?.state === 6 && !this.joined) {
      this.joined = snapshot.joined;
      this.handoff = new HandoffProtocol(
        this.joined,
        (type, payload) => this.signaling.sendPacket(type, payload),
        (id) => {
          void this.changeHost(id).catch((error) => this.fail(error));
        },
        this.options.onProgress,
      );
      await this.connectPeer();
    } else if (
      (type === AcdsType.SDP || type === AcdsType.ICE) &&
      this.peer &&
      this.joined
    ) {
      // ACDS broadcasts can include other participants' ICE candidates.
      if (
        payload.length >= 48 &&
        (!this.joined.hostId.every(
          (value, index) => payload[16 + index] === value,
        ) ||
          (payload.subarray(32, 48).some(Boolean) &&
            !this.joined.participantId.every(
              (value, index) => payload[32 + index] === value,
            )))
      )
        return;
      if (type === AcdsType.SDP) {
        const description = parseSignal(
          payload,
          this.joined,
          false,
        ) as RTCSessionDescriptionInit;
        if (description.type !== "answer")
          throw new Error("Expected an SDP answer from the native host");
        await this.peer.setRemoteDescription(description);
        for (const candidate of this.pendingIce.splice(0))
          await this.peer.addIceCandidate(candidate);
      } else {
        const candidate = parseSignal(
          payload,
          this.joined,
          true,
        ) as RTCIceCandidateInit;
        if (this.peer.remoteDescription)
          await this.peer.addIceCandidate(candidate);
        else this.pendingIce.push(candidate);
      }
    }
  }

  private async connectPeer(): Promise<void> {
    if (!this.joined) throw new Error("Missing discovery identity");
    this.options.onProgress?.("Negotiating WebRTC connection");
    this.peer = new RTCPeerConnection(
      discoveryRTCConfiguration(this.options, this.joined),
    );
    this.peer.onicecandidate = (event) => {
      if (!event.candidate || !this.joined || this.closed) return;
      try {
        this.signaling.sendPacket(
          AcdsType.ICE,
          signalPacket(
            this.joined,
            "ice",
            event.candidate.candidate,
            event.candidate.sdpMid || "0",
          ),
        );
      } catch (error) {
        this.fail(error);
      }
    };
    this.peer.onconnectionstatechange = () => {
      if (this.closed || !this.peer) return;
      if (
        this.peer.connectionState === "failed" ||
        this.peer.connectionState === "closed"
      ) {
        this.waitForSuccessor();
      } else if (this.peer.connectionState === "disconnected") {
        this.options.onProgress?.("WebRTC connection interrupted");
        if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
        this.disconnectedTimer = setTimeout(() => {
          this.disconnectedTimer = null;
          if (!this.closed && this.peer?.connectionState === "disconnected")
            this.fail(new Error("WebRTC connectivity was lost; reconnecting."));
        }, 5000);
      } else if (this.peer.connectionState === "connected") {
        if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
        this.disconnectedTimer = null;
        this.options.onProgress?.("Connected over WebRTC (DTLS encrypted)");
      }
    };
    const channel = this.peer.createDataChannel("acip", {
      ordered: true,
      negotiated: true,
      id: 0,
    });
    this.peerCrypto = await PeerCryptoSession.create(
      {
        ...this.options.cryptoOptions,
        password: this.options.cryptoOptions?.password || this.options.password,
      },
      this.options.applicationEncryption,
      false,
      (packet) => {
        if (!this.bridge) throw new Error("WebRTC bridge is not ready");
        this.bridge.send(packet);
      },
    );
    if (this.closed) {
      this.peerCrypto.close();
      this.peerCrypto = null;
      return;
    }
    channel.onopen = () => {
      if (this.closed) {
        channel.close();
        return;
      }
      let receivedAsciiFrames = 0;
      this.bridge = new WebRTCBridge(
        channel,
        (bytes) => {
          try {
            const decoded = this.peerCrypto!.receive(bytes);
            if (!decoded) return;
            bytes = decoded;
            const packet = parsePacket(bytes);
            if (packet.type === PacketType.PING)
              this.sendPacket(PacketType.PONG, bytes.slice(22));
            else {
              if (packet.type === PacketType.ASCII_FRAME) {
                receivedAsciiFrames++;
                if (receivedAsciiFrames % 60 === 0)
                  console.info(
                    "[WebRTCSession] ASCII_FRAME dispatch",
                    JSON.stringify({
                      count: receivedAsciiFrames,
                      callbackAttached: Boolean(this.packetCallback),
                      payloadBytes: bytes.length - 22,
                    }),
                  );
              }
              // Do not send browser capabilities at DataChannel open: the
              // native host may still be replacing its peer-manager
              // callback with the transport callback, which drops those
              // first packets. SERVER_STATE is emitted after registration.
              // A legacy/native host may begin streaming before emitting
              // SERVER_STATE. The first valid ASCII frame proves the data
              // channel and protocol callback are ready, so do not leave
              // the browser stuck in the connection timeout.
              if (
                packet.type === PacketType.SERVER_STATE ||
                packet.type === PacketType.ASCII_FRAME
              )
                this.completeConnection();
              this.packetCallback?.(packet, bytes.slice(22));
            }
          } catch (error) {
            this.fail(error);
          }
        },
        () => this.waitForSuccessor(),
        this.peer?.sctp?.maxMessageSize,
      );
      try {
        this.peerCrypto!.flush();
      } catch (error) {
        this.fail(error);
      }
    };
    channel.onclose = () => {
      if (!this.closed) this.waitForSuccessor();
    };
    channel.onerror = () => {
      if (!this.closed) this.waitForSuccessor();
    };
    const offer = await this.peer.createOffer();
    await this.peer.setLocalDescription(offer);
    if (this.closed) return;
    this.signaling.sendPacket(
      AcdsType.SDP,
      signalPacket(
        this.joined,
        "offer",
        `${offer.sdp!}a=dcmap:0 label="acip"\r\n`,
      ),
    );
  }
  private closePeer(): void {
    this.outbound?.close();
    this.outbound = null;
    this.peerCrypto?.close();
    this.peerCrypto = null;
    if (this.peer) {
      this.peer.onconnectionstatechange = null;
      this.peer.onicecandidate = null;
    }
    this.bridge?.close();
    this.bridge = null;
    this.peer?.close();
    this.peer = null;
    this.pendingIce = [];
  }
  private async changeHost(id: Uint8Array): Promise<void> {
    if (!this.joined || this.closed) return;
    this.setState(ConnectionState.CONNECTING);
    this.options.onProgress?.("Migrating to elected successor");
    if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
    this.disconnectedTimer = null;
    this.closePeer();
    this.joined.hostId = id;
    if (id.every((value, i) => value === this.joined!.participantId[i])) {
      const handoff = this.handoff;
      this.handoff = null;
      this.promoted = BrowserHostSession.adopt(
        this.options,
        this.signaling,
        this.joined,
        handoff ?? undefined,
      );
      this.promoted.onPacketReceived((packet, payload) =>
        this.packetCallback?.(packet, payload),
      );
      this.promoted.onStateChange((state) => this.setState(state));
      this.setState(this.promoted.getState());
    } else await this.connectPeer();
  }
  private waitForSuccessor(): void {
    if (this.closed || this.promoted) return;
    this.setState(ConnectionState.CONNECTING);
    this.options.onProgress?.(
      "Host disconnected — waiting for elected successor",
    );
    if (this.disconnectedTimer) return;
    this.disconnectedTimer = setTimeout(() => {
      this.disconnectedTimer = null;
      this.fail(
        new Error("No prepared successor became available within 15 seconds."),
      );
    }, 15000);
  }
  private setState(state: ConnectionState): void {
    this.state = state;
    this.stateCallback?.(state);
  }
  private clearTimer(): void {
    if (this.timer) clearTimeout(this.timer);
    this.timer = null;
  }
  private fail(error: unknown): void {
    if (this.closed) return;
    if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
    this.disconnectedTimer = null;
    const failure = error instanceof Error ? error : new Error(String(error));
    if (failure instanceof SessionRejectedError) this.canReconnect = false;
    this.rejectConnect?.(failure);
    this.rejectConnect = null;
    this.disconnect();
    this.setState(ConnectionState.ERROR);
    this.options.onProgress?.(failure.message);
  }
  getBufferedAmount(): number {
    return (
      this.promoted?.getBufferedAmount() ??
      (this.bridge?.getBufferedAmount() ?? 0) +
        (this.outbound?.snapshot().applicationBytes ?? 0)
    );
  }

  private outbound: OutboundQueue | null = null;
  getUploadFeedback() {
    if (this.promoted) return this.promoted.getUploadFeedback();
    const snapshot = this.outbound?.snapshot();
    return {
      bufferedBytes: this.getBufferedAmount(),
      oldestFrameAgeMs: snapshot?.oldestFrameAgeMs ?? 0,
      droppedFrames: snapshot?.dropped ?? 0,
    };
  }
  needsKeyframe(type: number): boolean {
    return (
      this.promoted?.needsKeyframe(type) ??
      this.outbound?.needsKeyframe(type) ??
      true
    );
  }
  sendPacket(type: number, payload: Uint8Array): void {
    if (this.promoted) {
      this.promoted.sendPacket(type, payload);
      return;
    }
    if (!this.bridge || this.state !== ConnectionState.CONNECTED)
      throw new Error("WebRTC is not connected");
    if (!this.outbound)
      this.outbound = new OutboundQueue(
        () => this.bridge?.getBufferedAmount() ?? 0,
        (packet) => this.peerCrypto!.sendPacket(packet),
        () => {
          // A failed media queue must not discard the independent ACDS
          // connection needed to commit a successor. Stop this peer and its
          // producers, then use the same bounded recovery as bridge failure.
          this.closePeer();
          this.waitForSuccessor();
        },
      );
    const admission = this.outbound.submit(serializePacket(type, payload, 0));
    if (
      admission === "congested" &&
      type !== PacketType.IMAGE_FRAME &&
      type !== PacketType.ASCII_FRAME &&
      type !== PacketType.IMAGE_FRAME_H264 &&
      type !== PacketType.IMAGE_FRAME_H265
    )
      throw new Error("Reliable outbound queue is congested");
  }
  sendUnencryptedAcipPacket(type: number, payload: Uint8Array): void {
    this.sendPacket(type, payload);
  }
  onStateChange(callback: (state: ConnectionState) => void): void {
    this.stateCallback = callback;
  }
  onPacketReceived(
    callback: (packet: ParsedPacket, payload: Uint8Array) => void,
  ): void {
    this.packetCallback = callback;
  }
  getState(): ConnectionState {
    return this.state;
  }
  getPublicKey(): string | null {
    return null;
  }
  disconnect(): void {
    this.outbound?.close();
    this.outbound = null;
    if (this.closed) return;
    this.closed = true;
    this.handoff?.close();
    this.promoted?.disconnect();
    if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
    this.disconnectedTimer = null;
    this.clearTimer();
    this.rejectConnect?.(new Error("Connection cancelled"));
    this.rejectConnect = null;
    try {
      this.discovery?.close();
    } catch {
      /* Signaling may already be closed. */
    }
    this.discovery = null;
    this.bridge?.close();
    this.bridge = null;
    this.peerCrypto?.close();
    this.peerCrypto = null;
    if (this.peer) {
      this.peer.onconnectionstatechange = null;
      this.peer.onicecandidate = null;
      this.peer.close();
    }
    this.peer = null;
    this.pendingIce = [];
    this.signaling.disconnect();
    this.setState(ConnectionState.DISCONNECTED);
  }
}
