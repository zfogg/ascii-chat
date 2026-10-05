import type { PacketTransport } from "./Transport";

/** ACIP uses the same byte stream framing on native DataChannels and WebSockets. */
export class WebRTCBridge implements PacketTransport {
  private pending = new Uint8Array(0);
  private queue: Array<{
    packet: Uint8Array;
    offset: number;
    replaceable: boolean;
  }> = [];
  private queuedBytes = 0;
  private readonly limit = 8 * 1024 * 1024;
  private closed = false;

  constructor(
    private channel: RTCDataChannel,
    private onPacket: (packet: Uint8Array) => void,
    private onError: (error: Error) => void,
    private maxMessageSize = 16384,
  ) {
    channel.binaryType = "arraybuffer";
    channel.bufferedAmountLowThreshold = 65536;
    channel.onbufferedamountlow = () => this.flush();
    channel.onmessage = (event: MessageEvent<ArrayBuffer>) => {
      try {
        const bytes = new Uint8Array(event.data);
        if (this.pending.length + bytes.length > this.limit)
          throw new Error("ACIP receive buffer exceeded");
        const merged = new Uint8Array(this.pending.length + bytes.length);
        merged.set(this.pending);
        merged.set(bytes, this.pending.length);
        let offset = 0;
        while (merged.length - offset >= 22) {
          const length =
            new DataView(merged.buffer, offset).getUint32(10, false) + 22;
          if (length > this.limit)
            throw new Error("ACIP packet exceeds receive limit");
          if (merged.length - offset < length) break;
          this.onPacket(merged.slice(offset, offset + length));
          offset += length;
        }
        this.pending = merged.slice(offset);
      } catch (error) {
        this.onError(error instanceof Error ? error : new Error(String(error)));
        this.close();
      }
    };
    channel.onerror = () =>
      this.onError(new Error("WebRTC DataChannel failed"));
  }

  send(packet: Uint8Array, replaceable = false): void {
    if (!this.isConnected()) throw new Error("WebRTC DataChannel is not open");
    if (replaceable) {
      // Only whole, unsent raw frames can be replaced. A partially transmitted
      // ACIP packet must finish to preserve framing, and control packets stay.
      this.queue = this.queue.filter((entry) => {
        if (entry.replaceable && entry.offset === 0) {
          this.queuedBytes -= entry.packet.length;
          return false;
        }
        return true;
      });
    }
    if (
      this.queuedBytes + this.channel.bufferedAmount + packet.length >
      this.limit
    ) {
      throw new Error("WebRTC send buffer is full");
    }
    this.queue.push({ packet, offset: 0, replaceable });
    this.queuedBytes += packet.length;
    this.flush();
  }

  private flush(): void {
    if (!this.isConnected()) return;
    try {
      while (this.queue.length && this.channel.bufferedAmount < 262144) {
        const entry = this.queue[0]!;
        const chunkSize = Math.max(
          1,
          Math.min(16384, this.maxMessageSize || 16384),
        );
        const chunk = entry.packet.slice(
          entry.offset,
          entry.offset + chunkSize,
        );
        this.channel.send(chunk);
        entry.offset += chunk.length;
        this.queuedBytes -= chunk.length;
        if (entry.offset === entry.packet.length) this.queue.shift();
      }
    } catch (error) {
      this.onError(error instanceof Error ? error : new Error(String(error)));
      this.close();
    }
  }

  isConnected(): boolean {
    return !this.closed && this.channel.readyState === "open";
  }
  close(): void {
    this.closed = true;
    this.queue = [];
    this.queuedBytes = 0;
    this.pending = new Uint8Array(0);
    this.channel.onmessage = null;
    this.channel.onbufferedamountlow = null;
    this.channel.onerror = null;
    this.channel.close();
  }
}
