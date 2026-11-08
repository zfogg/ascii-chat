import type { MainModule as MirrorBindings } from "./generated/mirror-bindings";
import type { MainModule as ClientBindings } from "./generated/client-bindings";

/** Bindings shared by the mirror and client raster worker modules. */
export interface TerminalRendererModule extends Pick<
  MirrorBindings,
  "TerminalRendererBinding" | "initMirror" | "patchOptions"
> {
  initClient?: ClientBindings["initClient"];
}
