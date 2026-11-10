import type { Response } from "express";
import { getRendererWasm } from "./node.js";

type ExpressRequestHandler = (request: unknown, response: Response) => unknown;

export async function sendRendererWasm(response: Response): Promise<void> {
  const { body, headers } = await getRendererWasm();
  response.set(headers);
  response.send(body);
}

/** Ready-to-register Express route callback. */
export const rendererWasmHandler: ExpressRequestHandler = (
  _request,
  response,
) => sendRendererWasm(response);
