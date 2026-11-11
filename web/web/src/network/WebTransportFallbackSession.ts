import { ClientConnection } from "./ClientConnection";
import {
  WebTransportClientSession,
  type WebTransportClientOptions,
} from "./WebTransportClientSession";
import type { ClientSession } from "./Transport";
import { ConnectionState, type ParsedPacket } from "../wasm/client";

/** A configured, identity-pinned WSS fallback for a WebTransport connection. */
export class WebTransportFallbackSession implements ClientSession {
  private active: ClientSession | null = null;
  private cancelled = false;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private cancelPending: (() => void) | undefined;
  private connectPromise: Promise<void> | null = null;
  private onState: ((state: ConnectionState) => void) | null = null;
  private onPacket:
    | ((packet: ParsedPacket, payload: Uint8Array) => void)
    | null = null;

  constructor(
    private readonly options: WebTransportClientOptions,
    private readonly fallbackUrl: string,
    private readonly timeoutMs = 10000,
  ) {
    const primary = new URL(options.serverUrl);
    const fallback = new URL(fallbackUrl);
    if (primary.protocol !== "https:" || fallback.protocol !== "wss:")
      throw new Error("WebTransport fallback requires HTTPS and WSS endpoints");
    if (
      !options.cryptoOptions?.expectedServerPublicKeyText?.trim() ||
      options.applicationEncryption === false
    )
      throw new Error(
        "WebTransport fallback requires application encryption and a pinned server key",
      );
    if (!Number.isFinite(timeoutMs) || timeoutMs <= 0)
      throw new Error("Invalid WebTransport fallback timeout");
  }

  get transportType() {
    return this.active?.transportType ?? "webtransport";
  }
  get canReconnect() {
    return this.active?.canReconnect ?? true;
  }
  getPublicKey() {
    return this.active?.getPublicKey() ?? null;
  }
  getState() {
    return this.active?.getState() ?? ConnectionState.DISCONNECTED;
  }
  getBufferedAmount() {
    return this.active?.getBufferedAmount?.() ?? 0;
  }
  getUploadFeedback() {
    return (
      this.active?.getUploadFeedback?.() ?? {
        bufferedBytes: 0,
        oldestFrameAgeMs: 0,
        droppedFrames: 0,
      }
    );
  }
  needsKeyframe(type: number) {
    return this.active?.needsKeyframe?.(type) ?? true;
  }
  onStateChange(callback: (state: ConnectionState) => void) {
    this.onState = callback;
  }
  onPacketReceived(
    callback: (packet: ParsedPacket, payload: Uint8Array) => void,
  ) {
    this.onPacket = callback;
  }
  sendPacket(type: number, payload: Uint8Array) {
    this.requireActive().sendPacket(type, payload);
  }
  sendUnencryptedAcipPacket(type: number, payload: Uint8Array) {
    this.requireActive().sendUnencryptedAcipPacket(type, payload);
  }
  connect() {
    return (this.connectPromise ??= this.start());
  }
  disconnect() {
    this.cancelled = true;
    this.clearTimer();
    this.cancelPending?.();
    const current = this.active;
    this.active = null;
    current?.disconnect();
    this.onState?.(ConnectionState.DISCONNECTED);
  }
  private requireActive() {
    if (!this.active) throw new Error("Connection is not active");
    return this.active;
  }
  private attach(session: ClientSession) {
    this.active = session;
    session.onStateChange((state) => {
      if (this.active === session && !this.cancelled) this.onState?.(state);
    });
    session.onPacketReceived((packet, payload) => {
      if (this.active === session && !this.cancelled)
        this.onPacket?.(packet, payload);
    });
  }
  private clearTimer() {
    if (this.timer !== undefined) clearTimeout(this.timer);
    this.timer = undefined;
  }
  private async start() {
    if (this.cancelled) return;
    if (typeof WebTransport !== "undefined") {
      let resolveTimeout: (() => void) | undefined;
      const timeout = new Promise<"timeout">((resolve) => {
        resolveTimeout = () => resolve("timeout");
      });
      this.cancelPending = resolveTimeout;
      const primary = new WebTransportClientSession({
        ...this.options,
        applicationEncryption: true,
        onTransportReady: () => {
          this.clearTimer();
          this.options.onTransportReady?.();
        },
      });
      this.attach(primary);
      this.timer = setTimeout(() => resolveTimeout?.(), this.timeoutMs);
      let result: "connected" | "timeout";
      try {
        result = await Promise.race([
          primary.connect().then(() => "connected" as const),
          timeout,
        ]);
      } finally {
        this.clearTimer();
        this.cancelPending = undefined;
      }
      if (this.cancelled || result === "connected") return;
      this.active = null;
      primary.disconnect();
    }
    if (this.cancelled) return;
    const fallback = new ClientConnection({
      serverUrl: this.fallbackUrl,
      applicationEncryption: true,
      ...(this.options.cryptoOptions
        ? { cryptoOptions: this.options.cryptoOptions }
        : {}),
      ...(this.options.width !== undefined
        ? { width: this.options.width }
        : {}),
      ...(this.options.height !== undefined
        ? { height: this.options.height }
        : {}),
    });
    this.attach(fallback);
    await fallback.connect();
  }
}
