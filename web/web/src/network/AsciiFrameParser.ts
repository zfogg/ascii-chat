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
 *   offset 24: frame data      (original_size bytes, UTF-8 ANSI string)
 */

export const ASCII_FRAME_HEADER_SIZE = 24;

export const FrameFlags = {
  HAS_COLOR: 0x01,
  IS_COMPRESSED: 0x02,
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

  if (header.flags & FrameFlags.IS_COMPRESSED) {
    // Compressed frames not yet supported
    const err = "Compressed ASCII frames not supported";
    console.error(`[AsciiFrameParser] ERROR: ${err}`);
    throw new Error(err);
  }

  if (ASCII_FRAME_HEADER_SIZE + originalSize > payload.length) {
    const err = `Frame payload size mismatch: header says ${originalSize} bytes but only ${payload.length - ASCII_FRAME_HEADER_SIZE} available`;
    console.error(`[AsciiFrameParser] ERROR: ${err}`);
    throw new Error(err);
  }

  const frameBytes = payload.slice(
    ASCII_FRAME_HEADER_SIZE,
    ASCII_FRAME_HEADER_SIZE + originalSize,
  );

  const decoder = new TextDecoder("utf-8");
  const ansiString = decoder.decode(frameBytes);

  return { header, ansiString };
}
