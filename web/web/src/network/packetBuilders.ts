import { H265Encoder } from "./H265Encoder";

export const CAPABILITIES_PACKET_SIZE = 168; // Includes codec capabilities (added 8 bytes)
export const STREAM_TYPE_VIDEO = 0x01;
export const STREAM_TYPE_AUDIO = 0x02;

// Codec capability bitmasks (must match include/ascii-chat/media/codecs.h)
export const VIDEO_CODEC_CAP_RGBA = 1 << 0; // Bit 0: RGBA support
export const VIDEO_CODEC_CAP_H265 = 1 << 1; // Bit 1: H.265/HEVC support

// Only advertise H.265 if browser supports WebCodecs encoding
export const VIDEO_CODEC_CAP_SUPPORTED = H265Encoder.isSupported()
  ? VIDEO_CODEC_CAP_RGBA | VIDEO_CODEC_CAP_H265
  : VIDEO_CODEC_CAP_RGBA;

export const AUDIO_CODEC_CAP_RAW = 1 << 0; // Bit 0: Raw PCM support
export const AUDIO_CODEC_CAP_OPUS = 1 << 1; // Bit 1: Opus support
export const AUDIO_CODEC_CAP_ALL = AUDIO_CODEC_CAP_RAW | AUDIO_CODEC_CAP_OPUS;

// Helper functions to map Settings types to WASM enums
export function buildStreamStartPacket(
  includeAudio: boolean = false,
): Uint8Array {
  const streamType = includeAudio
    ? STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO
    : STREAM_TYPE_VIDEO;
  const buf = new ArrayBuffer(4);
  const view = new DataView(buf);

  // Network byte order (big-endian)
  view.setUint32(0, streamType, false);

  const payload = new Uint8Array(buf);
  console.log(
    `[Client] STREAM_START payload: type=0x${streamType.toString(16)}, bytes=[${Array.from(
      payload,
    )
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );
  return payload;
}

export function buildCapabilitiesPacket(
  cols: number,
  rows: number,
  targetFps: number = 60,
): Uint8Array {
  const buf = new ArrayBuffer(CAPABILITIES_PACKET_SIZE);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Network byte order (big-endian) - server uses NET_TO_HOST_U32/U16 to read
  view.setUint32(0, 0x0f, false); // capabilities (color+utf8+etc)
  view.setUint32(4, 3, false); // color_level (truecolor)
  view.setUint32(8, 16777216, false); // color_count
  view.setUint32(12, 0, false); // render_mode (foreground)
  view.setUint16(16, cols, false); // width
  view.setUint16(18, rows, false); // height

  // term_type[32] at offset 20
  const termType = new TextEncoder().encode("xterm-256color");
  bytes.set(termType, 20);

  // colorterm[32] at offset 52
  const colorterm = new TextEncoder().encode("truecolor");
  bytes.set(colorterm, 52);

  bytes[84] = 1; // detection_reliable
  view.setUint32(85, 1, false); // utf8_support
  view.setUint32(89, 1, false); // palette_type (PALETTE_STANDARD=1)
  // palette_custom[64] at offset 93 - zeroed
  bytes[157] = Math.min(targetFps, 144); // desired_fps (0-144)
  bytes[158] = 0; // color_filter (none)
  bytes[159] = 1; // wants_padding

  // Codec capabilities (network byte order, big-endian)
  // Offset 160-163: video codec capabilities (RGBA always, H.265 if browser supports)
  view.setUint32(160, VIDEO_CODEC_CAP_SUPPORTED, false);
  // Offset 164-167: audio codec capabilities (supports Raw PCM, Opus)
  view.setUint32(164, AUDIO_CODEC_CAP_ALL, false);

  // Log capabilities packet structure for debugging
  console.log(
    `[Client] CAPABILITIES packet: size=${bytes.length}, width=${cols}, height=${rows}, video_caps=0x${VIDEO_CODEC_CAP_SUPPORTED.toString(
      16,
    )}, audio_caps=0x${AUDIO_CODEC_CAP_ALL.toString(16)}`,
  );
  console.log(
    `[Client] CAPABILITIES first 32 bytes: [${Array.from(bytes.slice(0, 32))
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );
  console.log(
    `[Client] CAPABILITIES last 8 bytes (codecs at offset 160-167): [${Array.from(
      bytes.slice(160, 168),
    )
      .map((b) => `0x${b.toString(16).padStart(2, "0")}`)
      .join(" ")}]`,
  );

  return bytes;
}

/**
 * Build IMAGE_FRAME payload: legacy 8-byte header + RGB24 pixel data.
 * Server expects: [width:4][height:4][rgb_data:w*h*3] (network byte order, big-endian)
 */
export function buildImageFramePayload(
  rgbaData: Uint8Array,
  width: number,
  height: number,
): Uint8Array {
  const pixelCount = width * height;
  const rgb24Size = pixelCount * 3;
  // image_frame_packet_t structure:
  // width(4) + height(4) + pixel_format(4) + compressed_size(4) + checksum(4) + timestamp(4) + rgb24_data
  const headerSize = 24;
  const totalSize = headerSize + rgb24Size;
  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Fill header (network byte order big-endian)
  view.setUint32(0, width, false); // width
  view.setUint32(4, height, false); // height
  view.setUint32(8, 3, false); // pixel_format: 3 = RGB24
  view.setUint32(12, 0, false); // compressed_size: 0 (not compressed)
  view.setUint32(16, 0, false); // checksum: 0 (TODO: calculate proper CRC32 if needed)
  view.setUint32(20, Date.now(), false); // timestamp: current time in milliseconds

  // Convert RGBA to RGB24 (strip alpha channel)
  let srcIdx = 0;
  let dstIdx = headerSize;
  for (let i = 0; i < pixelCount; i++) {
    const r = rgbaData[srcIdx] ?? 0;
    const g = rgbaData[srcIdx + 1] ?? 0;
    const b = rgbaData[srcIdx + 2] ?? 0;
    bytes[dstIdx] = r; // R
    bytes[dstIdx + 1] = g; // G
    bytes[dstIdx + 2] = b; // B
    srcIdx += 4;
    dstIdx += 3;
  }

  return bytes;
}

/**
 * Build IMAGE_FRAME_H265 payload: H.265 encoded video frame.
 * Server expects: [flags:u8][width:u16 BE][height:u16 BE][h265_data...]
 * flags: 0x01 = KEYFRAME, 0x02 = SIZE_CHANGE
 */
export function buildImageFrameH265Payload(
  flags: number,
  width: number,
  height: number,
  h265Data: Uint8Array,
): Uint8Array {
  const headerSize = 5; // 1 + 2 + 2 bytes
  const totalSize = headerSize + h265Data.byteLength;
  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Header: flags (1), width (2 BE), height (2 BE)
  view.setUint8(0, flags);
  view.setUint16(1, width, false); // big-endian
  view.setUint16(3, height, false); // big-endian

  // H.265 bitstream data
  bytes.set(h265Data, headerSize);

  return bytes;
}
