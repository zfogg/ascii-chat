import type { FastifyReply } from "fastify";
import { getRendererWasm } from "./node.js";

type FastifyRequestHandler = (request: unknown, reply: FastifyReply) => unknown;

export async function sendRendererWasm(reply: FastifyReply): Promise<void> {
  const { body, headers } = await getRendererWasm();
  reply.headers(headers).send(body);
}

/** Ready-to-register Fastify route callback. */
export const rendererWasmHandler: FastifyRequestHandler = (_request, reply) =>
  sendRendererWasm(reply);
