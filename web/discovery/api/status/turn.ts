import type { IncomingMessage, ServerResponse } from "node:http";
import { statusHandler } from "../_tcp";

export default async function handler(
  request: IncomingMessage,
  response: ServerResponse,
) {
  await statusHandler("turn", request, response);
}
