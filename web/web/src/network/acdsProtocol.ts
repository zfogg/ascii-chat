export const AcdsType = {
  LOOKUP: 6002,
  INFO: 6003,
  JOIN: 6004,
  JOINED: 6005,
  LEAVE: 6006,
  END: 6007,
  SDP: 6009,
  ICE: 6010,
  ERROR: 6199,
} as const;

const encoder = new TextEncoder();
/** A discovery rejection needs user action, not another connection attempt. */
export class SessionRejectedError extends Error {}
const decoder = new TextDecoder();
export function readString(
  bytes: Uint8Array,
  offset: number,
  length: number,
): string {
  const field = bytes.subarray(offset, offset + length);
  const end = field.indexOf(0);
  return decoder.decode(end < 0 ? field : field.subarray(0, end));
}
export interface JoinedSession {
  participantId: Uint8Array;
  sessionId: Uint8Array;
  hostId: Uint8Array;
  turnUsername: string;
  turnPassword: string;
}
export function signalPacket(
  session: JoinedSession,
  type: "offer" | "answer" | "ice",
  value: string,
  mid = "0",
): Uint8Array {
  const text = encoder.encode(value);
  const suffix =
    type === "ice" ? encoder.encode(`\0${mid}\0`) : new Uint8Array(0);
  if (text.length > 65535) throw new Error("Signaling message is too large");
  const header = type === "ice" ? 50 : 51;
  const bytes = new Uint8Array(header + text.length + suffix.length);
  bytes.set(session.sessionId);
  bytes.set(session.participantId, 16);
  bytes.set(session.hostId, 32);
  const view = new DataView(bytes.buffer);
  if (type === "ice") view.setUint16(48, text.length, false);
  else {
    bytes[48] = type === "offer" ? 0 : 1;
    view.setUint16(49, text.length, false);
  }
  bytes.set(text, header);
  bytes.set(suffix, header + text.length);
  return bytes;
}
export function parseSignal(
  bytes: Uint8Array,
  session: JoinedSession,
  ice: boolean,
): RTCSessionDescriptionInit | RTCIceCandidateInit {
  const header = ice ? 50 : 51;
  if (bytes.length < header) throw new Error("Truncated signaling message");
  const same = (offset: number, id: Uint8Array) =>
    id.every((value, index) => bytes[offset + index] === value);
  if (
    !same(0, session.sessionId) ||
    !same(16, session.hostId) ||
    (!same(32, session.participantId) && bytes.subarray(32, 48).some(Boolean))
  )
    throw new Error("Signaling participant mismatch");
  const length = new DataView(
    bytes.buffer,
    bytes.byteOffset,
    bytes.byteLength,
  ).getUint16(ice ? 48 : 49, false);
  if (!length || bytes.length < header + length)
    throw new Error("Invalid signaling length");
  const value = decoder.decode(bytes.subarray(header, header + length));
  if (ice)
    return {
      candidate: value,
      sdpMid: readString(bytes, header + length + 1, bytes.length) || "0",
    };
  if (bytes[48] !== 0 && bytes[48] !== 1) throw new Error("Invalid SDP type");
  return { type: bytes[48] === 0 ? "offer" : "answer", sdp: value };
}
