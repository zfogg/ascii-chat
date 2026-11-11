import { getClientModule } from "./client";
import type { AVStateBinding } from "@ascii-chat/shared/wasm";

/** Device APIs stay in the host; stream state and cancellation live in shared C. */
export class AVState {
  private binding: AVStateBinding | null = null;

  begin(): number {
    if (!this.binding) {
      const module = getClientModule();
      if (!module) throw new Error("Initialize the client before capture");
      const binding = new module.AVStateBinding();
      if (!binding.valid()) {
        binding.delete();
        throw new Error("Unable to allocate audio/video state");
      }
      this.binding = binding;
    }
    return this.binding.begin();
  }

  get generation(): number {
    return this.binding?.generation() ?? 0;
  }
  current(generation: number): boolean {
    return this.binding?.current(generation) ?? false;
  }
  complete(generation: number): boolean {
    return this.binding?.complete(generation) ?? false;
  }
  get active(): boolean {
    return this.binding?.active() ?? false;
  }

  stop(release: () => void): void {
    this.binding?.beginStop();
    release();
    this.binding?.finishStop();
  }

  /** Call only after pending host startup has settled and resources are stopped. */
  dispose(): void {
    this.binding?.delete();
    this.binding = null;
  }
}
