import { mkdirSync, writeFileSync } from "node:fs";
import { packetTypes } from "./wasm-protocol-version.mjs";

const output = `/** Generated from include/ascii-chat/network/packet/packet.h. Do not edit. */
const packetTypeDefaults = ${JSON.stringify(packetTypes, null, 2)} as const;
export const PacketType: { [K in keyof typeof packetTypeDefaults]: number } = {
  ...packetTypeDefaults,
};
export type PacketTypes = typeof PacketType;
export type PacketType = PacketTypes[keyof PacketTypes];

/** Replace generated defaults with the authoritative native WASM table. */
export function installPacketTypes(packetTypes: Readonly<Record<string, number>>): void {
  Object.assign(PacketType, packetTypes);
}
`;

mkdirSync(new URL("../web/packages/shared/src/wasm/", import.meta.url), {
  recursive: true,
});
writeFileSync(
  new URL("../web/packages/shared/src/wasm/protocol.ts", import.meta.url),
  output,
);
