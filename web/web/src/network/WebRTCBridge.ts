import type { PacketTransport } from "./Transport";
import { recordDownload, recordUpload } from "./bandwidth";

/** ACIP uses the same byte stream framing on native DataChannels and WebSockets. */
export class WebRTCBridge implements PacketTransport {
  private pending = new Uint8Array(0);
  private queue: Array<{ packet: Uint8Array; offset: number }> = [];
  private queuedBytes = 0;
  private readonly limit = 8 * 1024 * 1024;
  // Keep at most one current ASCII snapshot ahead of the receiver. A deeper
  // ordered backlog delays both rendered video and audio packets behind it.
  private readonly sendWindow = 256 * 1024;
  private closed = false;
  private receivedChunks = 0;
  private receivedBytes = 0;
  private receivedPackets = 0;
  private receivedPacketTypes: Record<number, number> = {};
  private lastReceiveReport = performance.now();

  constructor(
    private channel: RTCDataChannel,
    private onPacket: (packet: Uint8Array) => void,
    private onError: (error: Error) => void,
    private maxMessageSize = 16384,
  ) {
    channel.binaryType = "arraybuffer";
    // Resume queued writes halfway through the send window so frames and audio
    // do not wait for the channel buffer to drain almost completely.
    channel.bufferedAmountLowThreshold = this.sendWindow / 2;
    channel.onbufferedamountlow = () => this.flush();
    channel.onmessage = (event: MessageEvent<ArrayBuffer>) => {
      try {
        const bytes = new Uint8Array(event.data);
        recordDownload(bytes.byteLength);
        this.receivedChunks++;
        this.receivedBytes += bytes.length;
        // A full ACIP packet is the common case for browser-sized ASCII
        // frames. Avoid copying it into a temporary merged buffer before the
        // next animation frame; at 60 Hz those copies alone can starve the UI.
        if (this.pending.length === 0 && bytes.length >= 22) {
          const length =
            new DataView(
              bytes.buffer,
              bytes.byteOffset,
              bytes.byteLength,
            ).getUint32(10, false) + 22;
          if (length > this.limit)
            throw new Error("ACIP packet exceeds receive limit");
          if (length === bytes.length) {
            this.dispatchCompletePacket(bytes);
            return;
          }
        }
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
          const packet = merged.slice(offset, offset + length);
          this.dispatchCompletePacket(packet);
          offset += length;
        }
        this.pending = merged.slice(offset);
        const now = performance.now();
        if (now - this.lastReceiveReport >= 2000) {
          console.info(
            "[WebRTCBridge] receive " +
              JSON.stringify({
                chunks: this.receivedChunks,
                bytes: this.receivedBytes,
                packets: this.receivedPackets,
                types: this.receivedPacketTypes,
                pending: this.pending.length,
                bufferedAmount: channel.bufferedAmount,
              }),
          );
          this.receivedChunks = 0;
          this.receivedBytes = 0;
          this.receivedPackets = 0;
          this.receivedPacketTypes = {};
          this.lastReceiveReport = now;
        }
      } catch (error) {
        this.onError(error instanceof Error ? error : new Error(String(error)));
        this.close();
      }
    };
    channel.onerror = () =>
      this.onError(new Error("WebRTC DataChannel failed"));
  }

  private dispatchCompletePacket(packet: Uint8Array): void {
    this.receivedPackets++;
    const packetType = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    ).getUint16(8, false);
    this.receivedPacketTypes[packetType] =
      (this.receivedPacketTypes[packetType] ?? 0) + 1;
    this.onPacket(packet);
  }

  send(packet: Uint8Array): void {
    if (!this.isConnected()) throw new Error("WebRTC DataChannel is not open");
    if (
      this.queuedBytes + this.channel.bufferedAmount + packet.length >
      this.limit
    ) {
      throw new Error("WebRTC send buffer is full");
    }
    this.queue.push({ packet: packet.slice(), offset: 0 });
    this.queuedBytes += packet.length;
    this.flush();
  }

  private flush(): void {
    if (!this.isConnected()) return;
    try {
      while (this.queue.length) {
        const entry = this.queue[0]!;
        const highWaterMark = Math.max(this.sendWindow, entry.packet.length);
        if (
          entry.offset === 0 &&
          this.channel.bufferedAmount + entry.packet.length > highWaterMark
        ) {
          // A frame larger than the live window needs an empty channel. The
          // normal low-water event can fire too early and will not fire again
          // unless its threshold follows the remaining admission capacity.
          this.channel.bufferedAmountLowThreshold = Math.min(
            this.sendWindow / 2,
            highWaterMark - entry.packet.length,
          );
          break;
        }
        this.channel.bufferedAmountLowThreshold = this.sendWindow / 2;
        const chunkSize = Math.max(1, this.maxMessageSize || 16384);
        const chunk = entry.packet.slice(
          entry.offset,
          entry.offset + chunkSize,
        );
        this.channel.send(chunk);
        recordUpload(chunk.byteLength);
        entry.offset += chunk.length;
        this.queuedBytes -= chunk.length;
        if (entry.offset === entry.packet.length) this.queue.shift();
        if (this.channel.bufferedAmount >= highWaterMark) break;
      }
    } catch (error) {
      this.onError(error instanceof Error ? error : new Error(String(error)));
      this.close();
    }
  }

  getBufferedAmount(): number {
    return this.queuedBytes + this.channel.bufferedAmount;
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
