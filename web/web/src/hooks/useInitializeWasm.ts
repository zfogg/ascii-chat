import { useEffect } from "react";
import { ensureWasmModuleLoaded } from "../wasm/client";

/**
 * Hook that initializes the WASM module early in the app lifecycle.
 * This makes help text available for Settings tooltips before ClientConnection is created.
 */
export function useInitializeWasm(): void {
  useEffect(() => {
    ensureWasmModuleLoaded().catch((error) => {
      console.error(
        "[useInitializeWasm] Failed to initialize WASM module:",
        error,
      );
    });
  }, []);
}
