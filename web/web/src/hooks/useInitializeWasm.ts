import { useEffect } from "react";
import { useLocation } from "react-router-dom";

/** Mirror owns its module and help exports; only network routes need client WASM. */
export function useInitializeWasm(): void {
  const { pathname } = useLocation();
  useEffect(() => {
    if (pathname !== "/client" && pathname !== "/discovery") return;
    void import("../wasm/client")
      .then(({ ensureWasmModuleLoaded }) => ensureWasmModuleLoaded())
      .catch((error) =>
        console.error(
          "[useInitializeWasm] Failed to initialize WASM module:",
          error,
        ),
      );
  }, [pathname]);
}
