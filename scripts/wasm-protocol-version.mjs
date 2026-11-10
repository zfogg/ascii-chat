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
