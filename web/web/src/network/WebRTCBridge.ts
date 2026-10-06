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
  // Keep at most one current ASCII snapshot ahead of the receiver. A deeper
  // ordered backlog delays both rendered video and audio packets behind it.
  private readonly sendWindow = 256 * 1024;
  private closed = false;
  private receivedChunks = 0;
  private receivedBytes = 0;
  private receivedPackets = 0;
  private receivedPacketTypes: Record<number, number> = {};
  private lastReceiveReport = performance.now();
  // A server ASCII frame is a complete display snapshot. Keep only the newest
  // one until paint, while control and audio packets continue immediately.
  private latestAsciiFrame: Uint8Array | null = null;
  private asciiDispatchScheduled = false;

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
        this.receivedChunks++;
        this.receivedBytes += bytes.length;
        // A full ACIP packet is the common case for browser-sized ASCII
        // frames. Avoid copying it into a temporary merged buffer before the
        // next animation frame; at 60 Hz those copies alone can starve the UI.
        if (this.pending.length === 0 && bytes.length >= 22) {
          const length = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(10, false) + 22;
          if (length > this.limit) throw new Error("ACIP packet exceeds receive limit");
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
    const packetType = new DataView(packet.buffer, packet.byteOffset, packet.byteLength).getUint16(8, false);
    this.receivedPacketTypes[packetType] = (this.receivedPacketTypes[packetType] ?? 0) + 1;
    if (packetType === 3000) {
      this.latestAsciiFrame = packet;
      this.scheduleAsciiDispatch();
      return;
    }
    this.onPacket(packet);
  }

  private scheduleAsciiDispatch(): void {
    if (this.asciiDispatchScheduled) return;
    this.asciiDispatchScheduled = true;
    requestAnimationFrame(() => {
      this.asciiDispatchScheduled = false;
      const frame = this.latestAsciiFrame;
      this.latestAsciiFrame = null;
      if (frame && !this.closed) this.onPacket(frame);
    });
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
    const entry = { packet, offset: 0, replaceable };
    if (this.isAudioPacket(packet)) {
      // ACIP packets must remain whole and ordered, but audio can pass a video
      // frame that has not started yet. This prevents a waiting raw frame from
      // delaying audio while preserving any packet already on the wire.
      const nextUnsentFrame = this.queue.findIndex(
        (queued) => queued.replaceable && queued.offset === 0,
      );
      if (nextUnsentFrame >= 0) this.queue.splice(nextUnsentFrame, 0, entry);
      else this.queue.push(entry);
    } else {
      this.queue.push(entry);
    }
    this.queuedBytes += packet.length;
    this.flush();
  }

  private isAudioPacket(packet: Uint8Array): boolean {
    if (packet.length < 10) return false;
    const type = new DataView(
      packet.buffer,
      packet.byteOffset,
      packet.byteLength,
    ).getUint16(8, false);
    return type === 4000 || type === 4001;
  }

  private flush(): void {
    if (!this.isConnected()) return;
    try {
      while (this.queue.length) {
        const entry = this.queue[0]!;
        const isAudio = this.isAudioPacket(entry.packet);
        const highWaterMark = Math.max(this.sendWindow, entry.packet.length);
        // A small audio packet may bypass buffered video that has already
        // entered the DataChannel. An unsent video frame remains queued after
        // audio; partially sent ACIP packets still keep their byte order.
        // A raw browser image commonly exceeds the 256 KiB steady-state
        // window. Do not pause partway through that packet: the receiver
        // cannot use an incomplete ACIP frame, and draining it a 128 KiB low
        // watermark at a time leaves the server composing stale input. Keep
        // one complete replaceable image in flight; the next one still waits
        // until the DataChannel has room.
        if (this.channel.bufferedAmount >= highWaterMark && !isAudio) break;
        if (
          entry.replaceable &&
          entry.offset === 0 &&
          this.channel.bufferedAmount + entry.packet.length > highWaterMark
        )
          break;
        const chunkSize = Math.max(1, this.maxMessageSize || 16384);
        const chunk = entry.packet.slice(
          entry.offset,
          entry.offset + chunkSize,
        );
        this.channel.send(chunk);
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

  isConnected(): boolean {
    return !this.closed && this.channel.readyState === "open";
  }
  close(): void {
    this.closed = true;
    this.queue = [];
    this.queuedBytes = 0;
    this.pending = new Uint8Array(0);
    this.latestAsciiFrame = null;
    this.channel.onmessage = null;
    this.channel.onbufferedamountlow = null;
    this.channel.onerror = null;
    this.channel.close();
  }
}
