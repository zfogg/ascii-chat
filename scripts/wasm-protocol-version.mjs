import { readFileSync } from "node:fs";

// The native protocol constants are the authority for all WASM manifests.
const header = readFileSync(
  new URL("../include/ascii-chat/common/protocol_constants.h", import.meta.url),
  "utf8",
);
export const protocolVersion = ["MAJOR", "MINOR", "PATCH"]
  .map((part) => {
    const match = header.match(
      new RegExp(`^#define\\s+PROTOCOL_VERSION_${part}\\s+(\\d+)\\s*$`, "m"),
    );
    if (!match)
      throw new Error(
        `Missing protocol version ${part} in protocol_constants.h`,
      );
    return match[1];
  })
  .join(".");

const packetHeader = readFileSync(
  new URL("../include/ascii-chat/network/packet/packet.h", import.meta.url),
  "utf8",
);

/** Packet numbers are generated from the native enum. */
export const packetTypes = Object.fromEntries(
  [
    ...packetHeader.matchAll(
      /^\s*PACKET_TYPE_([A-Z0-9_]+)\s*=\s*(\d+)\s*,?\s*$/gm,
    ),
  ].map(([, name, value]) => [name, Number(value)]),
);
