import { mkdirSync, writeFileSync } from "node:fs";
import { packetTypes } from "./wasm-protocol-version.mjs";

const output = `/** Generated from include/ascii-chat/network/packet/packet.h. Do not edit. */
export const PacketType = ${JSON.stringify(packetTypes, null, 2)} as const;
export type PacketType = (typeof PacketType)[keyof typeof PacketType];
`;

mkdirSync(new URL("../web/packages/shared/src/wasm/", import.meta.url), {
  recursive: true,
});
writeFileSync(
  new URL("../web/packages/shared/src/wasm/protocol.ts", import.meta.url),
  output,
);
