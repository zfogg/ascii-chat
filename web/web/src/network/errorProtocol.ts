const ERROR_HEADER_SIZE = 8;
import {
  formatReportForProtocol,
  reportProtocolError,
  type BrowserErrorReport,
} from "../errors/errorBus";
import { getClientModule } from "../wasm/client";

/** Values from network_error_action_t; policy stays in the shared C engine. */
export enum RemoteErrorAction {
  Retry,
  Stop,
  Continue,
}

export function presentRemoteError(
  payload: Uint8Array,
  origin?: string,
): RemoteErrorAction {
  try {
    const error = decodeRemoteError(payload);
    const binding = getClientModule()!.ClientSessionBinding as unknown as {
      errorAction(code: number): number;
    };
    const action = binding.errorAction(error.code);
    reportProtocolError(error.code, error.message, origin);
    return action;
  } catch (error) {
    reportProtocolError(44, (error as Error).message, origin);
    return RemoteErrorAction.Stop;
  }
}

const ERROR_INTERNAL = 53;
const MAX_ERROR_MESSAGE_LENGTH = 4096;

export function encodeBrowserError(report: BrowserErrorReport): Uint8Array {
  const encoded = new TextEncoder().encode(formatReportForProtocol(report));
  const message = encoded.subarray(0, MAX_ERROR_MESSAGE_LENGTH);
  const payload = new Uint8Array(ERROR_HEADER_SIZE + message.length);
  const view = new DataView(payload.buffer);
  view.setUint32(0, ERROR_INTERNAL, false);
  view.setUint32(4, message.length, false);
  payload.set(message, ERROR_HEADER_SIZE);
  return payload;
}

export function decodeRemoteError(payload: Uint8Array): {
  code: number;
  message: string;
} {
  if (payload.length < ERROR_HEADER_SIZE)
    throw new Error("Remote error packet is truncated");
  const view = new DataView(
    payload.buffer,
    payload.byteOffset,
    payload.byteLength,
  );
  const code = view.getUint32(0, false);
  const length = view.getUint32(4, false);
  if (
    length > MAX_ERROR_MESSAGE_LENGTH ||
    length !== payload.length - ERROR_HEADER_SIZE
  )
    throw new Error("Remote error message length is invalid");
  return {
    code,
    message: new TextDecoder().decode(
      payload.subarray(ERROR_HEADER_SIZE, ERROR_HEADER_SIZE + length),
    ),
  };
}
