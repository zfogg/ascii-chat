import type { ConnectionState, ParsedPacket } from "../wasm/client";

export interface PacketTransport {
  getBufferedAmount?(): number;
  send(packet: Uint8Array): void;
  close(): void;
  isConnected(): boolean;
}

export interface ClientSession {
  videoCodecCapabilities?: number;
  getBufferedAmount?(): number;
  readonly transportType?: "websocket" | "webrtc";
  connect(): Promise<void>;
  disconnect(): void;
  getPublicKey(): string | null;
  getState(): ConnectionState;
  sendPacket(type: number, payload: Uint8Array): void;
  sendUnencryptedAcipPacket(type: number, payload: Uint8Array): void;
  onStateChange(callback: (state: ConnectionState) => void): void;
  onPacketReceived(
    callback: (packet: ParsedPacket, payload: Uint8Array) => void,
  ): void;
}
