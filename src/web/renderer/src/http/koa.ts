import { getRendererWasm } from "./node.js";
import type { Context } from "koa";

export async function setRendererWasm(ctx: Context): Promise<void> {
  const { body, headers } = await getRendererWasm();
  ctx.set(headers);
  ctx.body = body;
}

/** Ready-to-register Koa middleware callback. */
export const rendererWasmHandler = (ctx: Context): Promise<void> =>
  setRendererWasm(ctx);
