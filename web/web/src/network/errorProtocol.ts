import {
  formatReportForProtocol,
  type BrowserErrorReport,
} from "../errors/errorBus";

const ERROR_INTERNAL = 53;
const MAX_ERROR_MESSAGE_LENGTH = 4096;

export function encodeBrowserError(report: BrowserErrorReport): Uint8Array {
  const encoded = new TextEncoder().encode(formatReportForProtocol(report));
  const message = encoded.subarray(0, MAX_ERROR_MESSAGE_LENGTH);
  const payload = new Uint8Array(8 + message.length);
  const view = new DataView(payload.buffer);
  view.setUint32(0, ERROR_INTERNAL, false);
  view.setUint32(4, message.length, false);
  payload.set(message, 8);
  return payload;
}

export function decodeRemoteError(payload: Uint8Array): {
  code: number;
  message: string;
} {
  if (payload.length < 8) throw new Error("Remote error packet is truncated");
  const view = new DataView(
    payload.buffer,
    payload.byteOffset,
    payload.byteLength,
  );
  const code = view.getUint32(0, false);
  const length = view.getUint32(4, false);
  if (length > payload.length - 8)
    throw new Error("Remote error message length is invalid");
  return {
    code,
    message: new TextDecoder().decode(payload.subarray(8, 8 + length)),
  };
}
