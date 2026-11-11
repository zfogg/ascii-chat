import { AcipStreamDecoder } from "./AcipStreamDecoder";

export type AcipFlow = 0 | 1 | 2;

/** Decode the ACQS preamble and framed packets on one QUIC media stream. */
export class AcipQuicStreamDecoder {
  private readonly preamble = new Uint8Array(8);
  private preambleUsed = 0;
  private readonly packets = new AcipStreamDecoder();
  private flow: AcipFlow | null = null;
  private failed = false;

  feed(
    input: Uint8Array,
    emit: (flow: AcipFlow, packet: Uint8Array) => void,
    onFlow?: (flow: AcipFlow) => void,
  ): void {
    if (this.failed) throw new Error("ACIP QUIC stream decoder has failed");
    let offset = 0;
    if (this.flow === null) {
      const take = Math.min(
        this.preamble.length - this.preambleUsed,
        input.length,
      );
      this.preamble.set(input.subarray(0, take), this.preambleUsed);
      this.preambleUsed += take;
      offset += take;
      if (this.preambleUsed < this.preamble.length) return;
      const p = this.preamble;
      if (
        p[0] !== 65 ||
        p[1] !== 67 ||
        p[2] !== 81 ||
        p[3] !== 83 ||
        p[4] !== 1 ||
        (p[5] ?? 3) > 2 ||
        p[6] !== 0 ||
        p[7] !== 0
      ) {
        this.failed = true;
        throw new Error("Invalid ACIP QUIC stream preamble");
      }
      this.flow = p[5] as AcipFlow;
      try {
        onFlow?.(this.flow);
      } catch (error) {
        this.failed = true;
        throw error;
      }
    }
    const flow = this.flow;
    try {
      this.packets.feed(input.subarray(offset), (packet) => {
        const type = new DataView(
          packet.buffer,
          packet.byteOffset,
          packet.byteLength,
        ).getUint16(8);
        const packetFlow =
          type === 2001 || type === 4000 || type === 4001
            ? 1
            : type >= 3000 && type <= 3003
              ? 2
              : 0;
        // Encrypted packets must be checked against their flow after decryption.
        if (type !== 1200 && packetFlow !== flow)
          throw new Error("ACIP packet has the wrong flow");
        emit(flow, packet);
      });
    } catch (error) {
      this.failed = true;
      throw error;
    }
  }

  finish(): void {
    if (this.failed || this.flow === null)
      throw new Error("Truncated ACIP QUIC stream");
    this.packets.finish();
  }
}
