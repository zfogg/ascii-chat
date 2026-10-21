import { validateReceivedFrame } from "../wasm/client";

/**
 * Parse ascii_frame_packet_t from decrypted payload.
 *
 * Wire format (all uint32 fields in network byte order / big-endian via HOST_TO_NET_U32):
 *   offset 0:  width           (uint32)
 *   offset 4:  height          (uint32)
 *   offset 8:  original_size   (uint32)
 *   offset 12: compressed_size (uint32)
 *   offset 16: checksum        (uint32)
 *   offset 20: flags           (uint32)
 *   offset 24: optional versioned contract (flag 0x10), then encoded frame text.
 * See docs/security/rendered-frames.md. Text sizes/checksum exclude the contract.
 */

export const ASCII_FRAME_HEADER_SIZE = 24;
export const ASCII_FRAME_MAX_BYTES = 16 * 1024 * 1024;

export const FrameFlags = {
  HAS_COLOR: 0x01,
  IS_COMPRESSED: 0x02,
  RENDER_CONTRACT: 0x10,
} as const;

export interface AsciiFrameHeader {
  width: number;
  height: number;
  originalSize: number;
  compressedSize: number;
  checksum: number;
  flags: number;
}

export interface AsciiFrame {
  header: AsciiFrameHeader;
  ansiString: string;
}

export function parseAsciiFrame(payload: Uint8Array): AsciiFrame {
  if (payload.length < ASCII_FRAME_HEADER_SIZE) {
    const err = `ASCII frame payload too short: ${payload.length} < ${ASCII_FRAME_HEADER_SIZE}`;
    console.error(`[AsciiFrameParser] ERROR: ${err}`);
    throw new Error(err);
  }

  const view = new DataView(
    payload.buffer,
    payload.byteOffset,
    payload.byteLength,
  );

  // Network byte order (big-endian) - C code uses HOST_TO_NET_U32
  const width = view.getUint32(0, false);
  const height = view.getUint32(4, false);
  const originalSize = view.getUint32(8, false);
  const compressedSize = view.getUint32(12, false);
  const checksum = view.getUint32(16, false);
  const flags = view.getUint32(20, false);

  const header: AsciiFrameHeader = {
    width,
    height,
    originalSize,
    compressedSize,
    checksum,
    flags,
  };

  if (!originalSize || originalSize > ASCII_FRAME_MAX_BYTES)
    throw new Error("ASCII frame exceeds decoded byte budget");
  let offset = ASCII_FRAME_HEADER_SIZE;
  let metadata = payload.subarray(offset, offset);
  if (flags & FrameFlags.RENDER_CONTRACT) {
    if (payload.length - offset < 20)
      throw new Error("Truncated frame contract");
    const paletteBytes = view.getUint32(offset + 16, false);
    if (paletteBytes > 255 || payload.length - offset - 20 < paletteBytes)
      throw new Error("Truncated frame contract palette");
    metadata = payload.subarray(offset, offset + 20 + paletteBytes);
    offset += metadata.length;
  }
  const compressed = (header.flags & FrameFlags.IS_COMPRESSED) !== 0;
  const encodedSize = compressed ? compressedSize : originalSize;
  if (
    encodedSize > ASCII_FRAME_MAX_BYTES ||
    (compressed && compressedSize === 0) ||
    (!compressed && compressedSize !== 0) ||
    offset + encodedSize !== payload.length
  ) {
    const payloadDataSize = payload.length - offset;
    const err = `Frame payload size mismatch: header says ${encodedSize} bytes but payload has ${payloadDataSize}`;
    console.error(`[AsciiFrameParser] ERROR: ${err}`);
    throw new Error(err);
  }

  const encodedFrame = payload.subarray(offset, offset + encodedSize);
  const frameBytes = validateReceivedFrame(
    encodedFrame,
    width,
    height,
    checksum,
    flags,
    metadata,
    originalSize,
  );
  const decoder = new TextDecoder("utf-8", { fatal: true });
  const ansiString = decoder.decode(frameBytes);

  return { header, ansiString };
}
