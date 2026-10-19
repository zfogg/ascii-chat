import type { RuntimeValues } from "@ascii-chat/shared/wasm";
import type { ClientSession } from "./Transport";
import { acceptRemoteRenderSettings } from "../wasm/client";

export const SETTINGS_SYNC = 6063;
export const SETTINGS_ACK = 6064;
export interface RenderSettingsResult {
  request: number;
  revision: number;
  status: number;
  values: RuntimeValues;
}

/** Requests are scoped to a connection, so reconnects cannot consume stale replies. */
export class RenderSettingsControl {
  private request = 0;
  private lastAcceptedRequest = 0;
  private lastRevision = -1;
  constructor(
    private connection: ClientSession,
    private commitEffective: (
      payload: Uint8Array,
    ) => RuntimeValues = acceptRemoteRenderSettings,
  ) {}
  send(values: RuntimeValues, expectedRevision = 0xffff_ffff): number {
    const json = new TextEncoder().encode(JSON.stringify(values));
    if (json.length > 4096) throw new Error("Settings are too large");
    const payload = new Uint8Array(20 + json.length);
    const view = new DataView(payload.buffer);
    view.setUint32(0, 1);
    view.setUint32(4, ++this.request);
    view.setUint32(8, expectedRevision);
    payload.set(json, 20);
    this.connection.sendPacket(SETTINGS_SYNC, payload);
    return this.request;
  }
  receive(payload: Uint8Array): RenderSettingsResult | null {
    if (payload.length < 20 || payload.length > 4116)
      throw new Error("Invalid settings reply length");
    const view = new DataView(
      payload.buffer,
      payload.byteOffset,
      payload.byteLength,
    );
    if (view.getUint32(0) !== 1) throw new Error("Unsupported settings schema");
    const request = view.getUint32(4);
    const revision = view.getUint32(12);
    if (
      request === 0
        ? revision <= this.lastRevision
        : request !== this.request ||
          request <= this.lastAcceptedRequest ||
          revision < this.lastRevision
    )
      return null;
    const values = this.commitEffective(payload);
    if (request) this.lastAcceptedRequest = request;
    this.lastRevision = revision;
    return {
      request,
      revision: view.getUint32(12),
      status: view.getUint32(16),
      values: values as RuntimeValues,
    };
  }
}
