import type { IncomingMessage, ServerResponse } from "node:http";
import { statusHandler } from "../_tcp.js";

export default async function handler(
  request: IncomingMessage,
  response: ServerResponse,
) {
  await statusHandler("stun", request, response);
}
