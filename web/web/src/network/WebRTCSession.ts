import { ClientConnection } from "./ClientConnection";
import { WebRTCBridge } from "./WebRTCBridge";
import type { ClientSession } from "./Transport";
import {
  AcdsType,
  lookupRequest,
  joinRequest,
  parseJoined,
  parseSignal,
  signalPacket,
  readString,
  type JoinedSession,
} from "./acdsProtocol";
import {
  ConnectionState,
  PacketType,
  parsePacket,
  serializePacket,
  type ParsedPacket,
} from "../wasm/client";

export interface DiscoveryOptions {
  sessionName: string;
  password: string;
  signalingUrl: string;
  iceServers: RTCIceServer[];
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
  const iceServers = options.iceServers.flatMap((server) => {
    const urls = Array.isArray(server.urls) ? server.urls : [server.urls];
    return urls.flatMap((url): RTCIceServer[] => {
      if (!/^turns?:/i.test(url)) return [{ urls: url }];
      const credentials = options.turnUsername
        ? { username: options.turnUsername, credential: options.turnCredential }
        : server.username && server.credential
          ? { username: server.username, credential: server.credential }
          : { username: joined.turnUsername, credential: joined.turnPassword };
      const { username, credential } = credentials;
      return username && credential
        ? [{ urls: url, username, credential }]
        : [];
    });
  });
  if (
    options.iceTransportPolicy === "relay" &&
    !iceServers.some((server) => /^turns?:/i.test(String(server.urls)))
  )
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
  readonly transportType = "webrtc" as const;
  private signaling: ClientConnection;
  private peer: RTCPeerConnection | null = null;
  private bridge: WebRTCBridge | null = null;
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
      // ACDS relays plain ACIP signaling and skips the native crypto handshake.
      // The peer DataChannel remains protected by WebRTC DTLS.
      applicationEncryption: false,
      width,
      height,
    });
  }

  async connect(): Promise<void> {
    if (!!this.options.turnUsername !== !!this.options.turnCredential)
      throw new Error(
        "Provide both TURN username and password, or leave both blank.",
      );
    lookupRequest(this.options.sessionName);
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
          this.signaling.sendPacket(
            AcdsType.LOOKUP,
            lookupRequest(this.options.sessionName),
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
    if (type === AcdsType.INFO) {
      if (payload.length < 72) throw new Error("Truncated session information");
      if (!payload[0]) throw new Error("Session not found");
      if (payload[52] !== 1)
        throw new Error(
          "This session does not offer WebRTC. Start the native server with --webrtc.",
        );
      if (payload[70] || payload[71])
        throw new Error(
          "This session requires identity verification that this browser client does not yet support.",
        );
      if (payload[53] && !this.options.password)
        throw new Error("This session requires a password");
      this.options.onProgress?.("Joining session");
      this.signaling.sendPacket(
        AcdsType.JOIN,
        joinRequest(this.options.sessionName, this.options.password),
      );
    } else if (type === AcdsType.JOINED) {
      this.joined = parseJoined(payload);
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
        if (this.peer.connectionState === "failed" || this.peer.connectionState === "closed") {
          this.fail(
            new Error(
              "WebRTC connection failed. Check STUN/TURN settings and reconnect.",
            ),
          );
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
              const packet = parsePacket(bytes);
              if (packet.type === PacketType.ENCRYPTED)
                throw new Error(
                  "Unexpected custom encryption on WebRTC transport",
                );
              if (packet.type === PacketType.PING)
                this.sendPacket(PacketType.PONG, bytes.slice(22));
              else {
                if (packet.type === PacketType.ASCII_FRAME) {
                  receivedAsciiFrames++;
                  if (receivedAsciiFrames % 60 === 0)
                    console.info("[WebRTCSession] ASCII_FRAME dispatch", JSON.stringify({
                      count: receivedAsciiFrames,
                      callbackAttached: Boolean(this.packetCallback),
                      payloadBytes: bytes.length - 22,
                    }));
                }
                // Do not send browser capabilities at DataChannel open: the
                // native host may still be replacing its peer-manager
                // callback with the transport callback, which drops those
                // first packets. SERVER_STATE is emitted after registration.
                if (packet.type === PacketType.SERVER_STATE)
                  this.completeConnection();
                this.packetCallback?.(packet, bytes.slice(22));
              }
            } catch (error) {
              this.fail(error);
            }
          },
          (error) => this.fail(error),
          this.peer?.sctp?.maxMessageSize,
        );
      };
      channel.onclose = () => {
        if (!this.closed) this.fail(new Error("WebRTC DataChannel closed"));
      };
      channel.onerror = (event) =>
        this.fail(
          new Error(
            `WebRTC DataChannel failed: ${(event as RTCErrorEvent).error?.message || "unknown SCTP error"}`,
          ),
        );
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
    } else if (type === AcdsType.END || type === AcdsType.ERROR) {
      throw new Error(
        type === AcdsType.END
          ? "Session ended"
          : readString(payload, 2, 256) ||
              "Discovery service rejected the request",
      );
    }
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
    this.rejectConnect?.(failure);
    this.rejectConnect = null;
    this.disconnect();
    this.setState(ConnectionState.ERROR);
    this.options.onProgress?.(failure.message);
  }
  sendPacket(type: number, payload: Uint8Array): void {
    if (!this.bridge || this.state !== ConnectionState.CONNECTED)
      throw new Error("WebRTC is not connected");
    // DTLS provides encryption; ACIP media packets carry no custom encryption wrapper.
    this.bridge.send(
      serializePacket(type, payload, 0),
      type === PacketType.IMAGE_FRAME,
    );
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
    if (this.closed) return;
    this.closed = true;
    if (this.disconnectedTimer) clearTimeout(this.disconnectedTimer);
    this.disconnectedTimer = null;
    this.clearTimer();
    this.rejectConnect?.(new Error("Connection cancelled"));
    this.rejectConnect = null;
    if (
      this.joined &&
      this.signaling.getState() === ConnectionState.CONNECTED
    ) {
      try {
        const leave = new Uint8Array(32);
        leave.set(this.joined.sessionId);
        leave.set(this.joined.participantId, 16);
        this.signaling.sendPacket(AcdsType.LEAVE, leave);
      } catch {
        /* The discovery socket may already be closed. */
      }
    }
    this.bridge?.close();
    this.bridge = null;
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
