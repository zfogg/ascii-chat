import type { AcipFlow } from "./AcipQuicStreamDecoder";

const HEADER_SIZE = 28;
const MAX_WIRE_PACKET = 22 + 1024 * 1024 + 24 + 16;
const MAX_FRAGMENTS = 4096;
const FLOW_BUDGET = 2 * 1024 * 1024;
const FLOW_SLOTS = 4;
const EXPIRY_MS = 1000;
type MediaFlow = Exclude<AcipFlow, 0>;
/** Encode one ACQD fragment of an already encrypted ACIP packet. */
export function encodeAcipQuicDatagram(
  flow: MediaFlow,
  id: bigint,
  packet: Uint8Array,
  unit: number,
  index: number,
): Uint8Array {
  if (
    (flow !== 1 && flow !== 2) ||
    id <= 0n ||
    id > 0xffffffffffffffffn ||
    packet.length === 0 ||
    packet.length > MAX_WIRE_PACKET ||
    !Number.isInteger(unit) ||
    unit <= 0 ||
    unit > 0xffff ||
    !Number.isInteger(index) ||
    index < 0
  )
    throw new Error("Invalid datagram fragment parameters");
  const count = Math.ceil(packet.length / unit);
  if (count > MAX_FRAGMENTS || index >= count)
    throw new Error("Invalid datagram fragment count or index");
  const start = index * unit;
  const payload = packet.subarray(start, Math.min(start + unit, packet.length));
  const output = new Uint8Array(HEADER_SIZE + payload.length);
  output.set([65, 67, 81, 68, 1, flow]);
  const view = new DataView(output.buffer);
  view.setBigUint64(8, id);
  view.setUint32(16, packet.length);
  view.setUint16(20, unit);
  view.setUint16(22, index);
  view.setUint16(24, count);
  output.set(payload, HEADER_SIZE);
  return output;
}

type Slot = {
  flow: MediaFlow;
  id: bigint;
  started: number;
  total: number;
  unit: number;
  count: number;
  remaining: number;
  bytes: Uint8Array;
  received: Uint8Array;
};

/** Reassemble ACQD v1 fragments. The result must still be authenticated and
 * checked for replay before dispatch. */
export class AcipQuicDatagramDecoder {
  private slots: Slot[] = [];
  private lastTime = 0;

  expire(now: number): void {
    if (!Number.isFinite(now) || now < this.lastTime)
      throw new Error("Invalid datagram clock");
    this.lastTime = now;
    this.slots = this.slots.filter((slot) => now - slot.started < EXPIRY_MS);
  }

  reset(): void {
    this.slots = [];
    this.lastTime = 0;
  }

  feed(
    data: Uint8Array,
    now: number,
  ): { flow: MediaFlow; packet: Uint8Array } | null {
    if (
      data.length < HEADER_SIZE ||
      !Number.isFinite(now) ||
      now < this.lastTime
    )
      throw new Error("Invalid datagram input");
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const flow = data[5];
    const id = view.getBigUint64(8);
    const total = view.getUint32(16);
    const unit = view.getUint16(20);
    const index = view.getUint16(22);
    const count = view.getUint16(24);
    if (
      data[0] !== 65 ||
      data[1] !== 67 ||
      data[2] !== 81 ||
      data[3] !== 68 ||
      data[4] !== 1 ||
      (flow !== 1 && flow !== 2) ||
      data[6] !== 0 ||
      data[7] !== 0 ||
      data[26] !== 0 ||
      data[27] !== 0 ||
      id === 0n ||
      total === 0 ||
      total > MAX_WIRE_PACKET ||
      unit === 0 ||
      count === 0 ||
      count > MAX_FRAGMENTS ||
      count !== Math.ceil(total / unit) ||
      index >= count
    )
      throw new Error("Invalid datagram envelope");
    const offset = index * unit;
    const part = Math.min(unit, total - offset);
    if (data.length !== HEADER_SIZE + part)
      throw new Error("Invalid datagram fragment size");
    this.expire(now);
    let slot = this.slots.find(
      (candidate) => candidate.flow === flow && candidate.id === id,
    );
    if (
      slot &&
      (slot.total !== total || slot.unit !== unit || slot.count !== count)
    )
      throw new Error("Conflicting datagram fragments");
    if (!slot) {
      const sameFlow = this.slots.filter(
        (candidate) => candidate.flow === flow,
      );
      const cost = total + Math.ceil(count / 8);
      if (
        sameFlow.length >= FLOW_SLOTS ||
        sameFlow.reduce(
          (sum, item) => sum + item.bytes.length + item.received.length,
          0,
        ) +
          cost >
          FLOW_BUDGET
      )
        throw new Error("Datagram reassembly budget exhausted");
      slot = {
        flow,
        id,
        started: now,
        total,
        unit,
        count,
        remaining: count,
        bytes: new Uint8Array(total),
        received: new Uint8Array(Math.ceil(count / 8)),
      };
      this.slots.push(slot);
    }
    const bitmapIndex = index >> 3;
    const bit = 1 << (index & 7);
    const fragment = data.subarray(HEADER_SIZE);
    if ((slot.received[bitmapIndex]! & bit) !== 0) {
      if (fragment.some((value, i) => slot!.bytes[offset + i] !== value))
        throw new Error("Conflicting duplicate datagram fragment");
      return null;
    }
    slot.bytes.set(fragment, offset);
    slot.received[bitmapIndex] = slot.received[bitmapIndex]! | bit;
    if (--slot.remaining !== 0) return null;
    this.slots = this.slots.filter((candidate) => candidate !== slot);
    return { flow, packet: slot.bytes };
  }
}
