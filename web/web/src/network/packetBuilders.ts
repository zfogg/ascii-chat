export const CAPABILITIES_PACKET_SIZE = 168; // Includes codec capabilities (added 8 bytes)
export const STREAM_TYPE_VIDEO = 0x01;
export const STREAM_TYPE_AUDIO = 0x02;

// Codec capability bitmasks (must match include/ascii-chat/media/codecs.h)
export const VIDEO_CODEC_CAP_RGBA = 1 << 0; // Bit 0: RGBA support
export const VIDEO_CODEC_CAP_H265 = 1 << 1; // Bit 1: H.265/HEVC support

// HEVC is added per session only after probing the actual encoder configuration.
export const VIDEO_CODEC_CAP_SUPPORTED = VIDEO_CODEC_CAP_RGBA;

export const AUDIO_CODEC_CAP_RAW = 1 << 0; // Bit 0: Raw PCM support
export const AUDIO_CODEC_CAP_OPUS = 1 << 1; // Bit 1: Opus support
export const AUDIO_CODEC_CAP_ALL = AUDIO_CODEC_CAP_RAW | AUDIO_CODEC_CAP_OPUS;
const TERM_CAP_MATRIX_RAIN = 0x0020;

export type BrowserColorMode = "auto" | "none" | "16" | "256" | "truecolor";
export type BrowserColorFilter =
  | "none"
  | "black"
  | "white"
  | "green"
  | "magenta"
  | "fuchsia"
  | "orange"
  | "teal"
  | "cyan"
  | "pink"
  | "red"
  | "yellow"
  | "rainbow";
export type BrowserPalette =
  | "standard"
  | "blocks"
  | "digital"
  | "minimal"
  | "cool"
  | "custom";

const COLOR_FILTER_VALUES: Record<BrowserColorFilter, number> = {
  none: 0,
  black: 1,
  white: 2,
  green: 3,
  magenta: 4,
  fuchsia: 5,
  orange: 6,
  teal: 7,
  cyan: 8,
  pink: 9,
  red: 10,
  yellow: 11,
  rainbow: 12,
};

// These values match palette_type_t. PALETTE_UNSET is 0; Standard is 1.
const PALETTE_VALUES: Record<BrowserPalette, number> = {
  standard: 1,
  blocks: 2,
  digital: 3,
  minimal: 4,
  cool: 5,
  custom: 6,
};

function getColorCapabilities(colorMode: BrowserColorMode): {
  count: number;
  level: number;
} {
  switch (colorMode) {
    case "none":
      return { level: 0, count: 0 };
    case "16":
      return { level: 1, count: 16 };
    case "256":
      return { level: 2, count: 256 };
    case "auto":
    case "truecolor":
      // A canvas renderer does not inherit terminal capability limits.
      return { level: 3, count: 16_777_216 };
  }
}

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
  colorMode: BrowserColorMode = "truecolor",
  colorFilter: BrowserColorFilter = "none",
  palette: BrowserPalette = "standard",
  paletteChars?: string,
  matrixRain = false,
  videoCodecCapabilities = VIDEO_CODEC_CAP_SUPPORTED,
): Uint8Array {
  const buf = new ArrayBuffer(CAPABILITIES_PACKET_SIZE);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Network byte order (big-endian) - server uses NET_TO_HOST_U32/U16 to read
  view.setUint32(0, 0x0f | (matrixRain ? TERM_CAP_MATRIX_RAIN : 0), false);
  const color = getColorCapabilities(colorMode);
  view.setUint32(4, color.level, false); // color_level
  view.setUint32(8, color.count, false); // color_count
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
  view.setUint32(89, PALETTE_VALUES[palette], false); // palette_type
  if (palette === "custom" && paletteChars) {
    // palette_custom[64] at offset 93, null-padded by ArrayBuffer.
    bytes.set(new TextEncoder().encode(paletteChars).slice(0, 63), 93);
  }
  bytes[157] = Math.min(targetFps, 144); // desired_fps (0-144)
  bytes[158] = COLOR_FILTER_VALUES[colorFilter]; // color_filter
  bytes[159] = 1; // wants_padding

  // Codec capabilities (network byte order, big-endian)
  // Offset 160-163: video codec capabilities (RGBA always, H.265 if browser supports)
  view.setUint32(160, videoCodecCapabilities, false);
  // Offset 164-167: audio codec capabilities (supports Raw PCM, Opus)
  view.setUint32(164, AUDIO_CODEC_CAP_ALL, false);

  // Log capabilities packet structure for debugging
  console.log(
    `[Client] CAPABILITIES packet: size=${bytes.length}, width=${cols}, height=${rows}, color=${colorMode}, video_caps=0x${videoCodecCapabilities.toString(
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
 * Build IMAGE_FRAME payload: 24-byte native image_frame_packet_t header + RGB24 pixels.
 * Header fields and pixels use network byte order / packed RGB24 as in acip_send_image_frame().
 */
export function buildImageFramePayload(
  rgbaData: Uint8Array,
  width: number,
  height: number,
): Uint8Array {
  const pixelCount = width * height;
  const rgb24Size = pixelCount * 3;
  // Keep this layout in sync with image_frame_packet_t in packet.h.
  // The C sender uses pixel_format=1 for RGB24.
  const headerSize = 24;
  const totalSize = headerSize + rgb24Size;
  const buf = new ArrayBuffer(totalSize);
  const view = new DataView(buf);
  const bytes = new Uint8Array(buf);

  // Fill header (network byte order big-endian)
  view.setUint32(0, width, false); // width
  view.setUint32(4, height, false); // height
  view.setUint32(8, 1, false); // pixel_format (RGB24)
  view.setUint32(12, 0, false); // compressed_size (raw pixels)
  view.setUint32(16, 0, false); // checksum (unused by current handler)
  view.setUint32(20, 0, false); // timestamp (same as native sender)
  // Convert RGBA to RGB24 (strip alpha channel)
  let dstIdx = headerSize;
  for (let i = 0; i < pixelCount; i++) {
    const srcIdx = i * 4;
    const r = rgbaData[srcIdx] ?? 0;
    const g = rgbaData[srcIdx + 1] ?? 0;
    const b = rgbaData[srcIdx + 2] ?? 0;
    bytes[dstIdx] = r; // R
    bytes[dstIdx + 1] = g; // G
    bytes[dstIdx + 2] = b; // B
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
