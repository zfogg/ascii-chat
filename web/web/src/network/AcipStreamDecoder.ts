/** Incremental ACIP packet framing used by byte-stream transports. */
const HEADER_SIZE = 22;
const MAX_PAYLOAD = 5 * 1024 * 1024;
const MAGIC = [0, 0, 0, 0x0a, 0x5c, 0x11, 0xc4, 0xa1];
const CRC_TABLE = Uint32Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; ++bit)
    value = (value >>> 1) ^ (0x82f63b78 & -(value & 1));
  return value >>> 0;
});

function crc32(bytes: Uint8Array): number {
  let crc = 0xffffffff;
  for (const byte of bytes)
    crc = (crc >>> 8) ^ (CRC_TABLE[(crc ^ byte) & 0xff] ?? 0);
  return (crc ^ 0xffffffff) >>> 0;
}

export class AcipStreamDecoder {
  private readonly header = new Uint8Array(HEADER_SIZE);
  private headerUsed = 0;
  private packet: Uint8Array | null = null;
  private packetUsed = 0;
  private failed = false;
  private finished = false;

  feed(input: Uint8Array, emit: (packet: Uint8Array) => void): void {
    if (this.failed || this.finished)
      throw new Error("ACIP stream decoder is closed");
    let offset = 0;
    while (offset < input.length) {
      if (!this.packet) {
        const take = Math.min(
          HEADER_SIZE - this.headerUsed,
          input.length - offset,
        );
        this.header.set(input.subarray(offset, offset + take), this.headerUsed);
        this.headerUsed += take;
        offset += take;
        if (this.headerUsed < HEADER_SIZE) return;
        const view = new DataView(this.header.buffer);
        const length = view.getUint32(10);
        if (
          MAGIC.some((byte, index) => this.header[index] !== byte) ||
          length > MAX_PAYLOAD
        ) {
          this.failed = true;
          throw new Error("Invalid ACIP packet header");
        }
        this.packet = new Uint8Array(HEADER_SIZE + length);
        this.packet.set(this.header);
        this.packetUsed = HEADER_SIZE;
      }
      const packet = this.packet;
      const take = Math.min(
        packet.length - this.packetUsed,
        input.length - offset,
      );
      packet.set(input.subarray(offset, offset + take), this.packetUsed);
      this.packetUsed += take;
      offset += take;
      if (this.packetUsed !== packet.length) return;
      const checksum = new DataView(packet.buffer).getUint32(14);
      if (
        packet.length > HEADER_SIZE &&
        crc32(packet.subarray(HEADER_SIZE)) !== checksum
      ) {
        this.failed = true;
        throw new Error("Invalid ACIP packet checksum");
      }
      this.packet = null;
      this.packetUsed = 0;
      this.headerUsed = 0;
      emit(packet);
    }
  }

  finish(): void {
    if (this.failed || this.finished || this.headerUsed !== 0)
      throw new Error("Truncated ACIP stream");
    this.finished = true;
  }

  reset(): void {
    this.headerUsed = 0;
    this.packet = null;
    this.packetUsed = 0;
    this.failed = false;
    this.finished = false;
  }
}
