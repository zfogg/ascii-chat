import { useLayoutEffect } from "react";
import { type RefObject, useCallback, useEffect, useRef } from "react";
import { getMirrorModule, type MirrorModule } from "../../wasm/mirror";
import {
  getColorFilter,
  getColorMode,
  getFlipX,
  getFlipY,
  getMatrixRain,
  getPalette,
  getTargetFps,
} from "../../wasm";
import {
  wasmColorModeToString,
  wasmColorFilterToString,
} from "../../utils/colorMappers";

import { browserRasterFontSize } from "./rasterConfig";
import type { TerminalRendererBinding } from "../../wasm/generated/mirror-bindings";

const rendererFontSelection = (matrix: boolean) => (matrix ? 2 : 1);
type RendererConfig = { cols: number; rows: number; matrix: boolean };

interface UseInitAsciiRendererReturn {
  moduleRef: RefObject<MirrorModule | null>;
  setupDoneRef: RefObject<boolean>;
  rendererPtrRef: RefObject<TerminalRendererBinding | null>;
  resizeTimeoutRef: RefObject<ReturnType<typeof setTimeout> | null>;
  setUpdateDimensions: (fn: (cols: number, rows: number) => void) => void;
  triggerRendererRecreate: () => void;
}

interface UseInitAsciiRendererParams {
  canvasRef: RefObject<HTMLCanvasElement | null>;
  wasmModuleReady: boolean | undefined;
  initializeOptions?: boolean;
  matrixMode?: boolean;
  columns?: number;
  rows?: number;
}

export function useInitAsciiRenderer({
  canvasRef,
  wasmModuleReady,
  initializeOptions = true,
  matrixMode = false,
  columns = 0,
  rows = 0,
}: UseInitAsciiRendererParams): UseInitAsciiRendererReturn {
  const moduleRef = useRef<MirrorModule | null>(null);
  const setupDoneRef = useRef(false);
  const rendererPtrRef = useRef<TerminalRendererBinding | null>(null);
  const updateDimensionsRef = useRef<
    ((cols: number, rows: number) => void) | null
  >(null);
  const resizeObserverRef = useRef<ResizeObserver | null>(null);
  const resizeTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const pendingDimensionsRef = useRef<{ cols: number; rows: number } | null>(
    null,
  );
  const observedContainerSizeRef = useRef({ width: 0, height: 0 });
  const requestedGridRef = useRef({ columns, rows });
  useLayoutEffect(() => {
    requestedGridRef.current = { columns, rows };
  }, [columns, rows]);
  const currentMatrixModeRef = useRef<boolean>(false);
  const cellMetricsRef = useRef(
    new Map<string, { width: number; height: number }>(),
  );

  // Rebuild and apply args with current settings from WASM state
  const reinitializeWithCurrentSettings = useCallback(() => {
    if (!moduleRef.current) {
      console.warn(
        "[reinitializeWithCurrentSettings] moduleRef.current is null, skipping reinitialization",
      );
      return;
    }

    try {
      // Get current rendering settings that affect the renderer appearance
      const matrixRain = getMatrixRain();
      const colorMode = getColorMode();
      const colorFilter = getColorFilter();
      const palette = getPalette();
      const flipX = getFlipX();
      const flipY = getFlipY();
      const targetFps = getTargetFps();

      // Reconstruct args string with rendering settings
      const args = ["mirror"];
      if (matrixRain) {
        args.push("--matrix");
      }
      args.push(`--color-mode`, wasmColorModeToString(colorMode));
      args.push(`--color-filter`, wasmColorFilterToString(colorFilter));
      args.push(`--palette`, palette);
      if (flipX) args.push("--flip-x");
      if (flipY) args.push("--flip-y");
      args.push(`--fps`, String(targetFps));

      const argsStr = args.join(" ");
      console.debug(
        "[reinitializeWithCurrentSettings] Calling mirror_init_with_args with:",
        argsStr,
      );

      const initResult = moduleRef.current.initMirror(argsStr);
      if (initResult !== 0)
        throw new Error(`Failed to reinitialize mirror C code: ${initResult}`);
    } catch (err) {
      console.error(
        "[reinitializeWithCurrentSettings] Error reapplying settings:",
        err,
      );
    }
  }, []);

  // Load WASM module when ready
  useEffect(() => {
    if (wasmModuleReady) {
      const module = getMirrorModule();
      moduleRef.current = module;
    }
  }, [wasmModuleReady]);

  // Track matrix mode so resize can recreate renderer with correct font
  useEffect(() => {
    currentMatrixModeRef.current = matrixMode;
  }, [matrixMode]);

  // Embind owns the renderer and builds its configuration on the C side.
  const callRendererCreate = useCallback((config: RendererConfig) => {
    const module = moduleRef.current;
    if (!module) throw new Error("WASM renderer module is unavailable");
    const renderer = new module.TerminalRendererBinding(
      config.cols,
      config.rows,
      browserRasterFontSize(config.matrix),
      config.matrix,
      rendererFontSelection(config.matrix),
    );
    if (!renderer.valid()) {
      const code = renderer.error();
      renderer.delete();
      throw new Error(`term_renderer_create failed with error code ${code}`);
    }
    return renderer;
  }, []);

  const createConfigStruct = useCallback(
    (containerWidth: number, containerHeight: number, isMatrixMode = false) => {
      const module = moduleRef.current;
      if (!module) throw new Error("WASM renderer module is unavailable");
      const selection = rendererFontSelection(isMatrixMode);
      const metricKey = `${selection}`;
      let cell = cellMetricsRef.current.get(metricKey);
      if (!cell) {
        const probe = callRendererCreate({
          cols: 1,
          rows: 1,
          matrix: isMatrixMode,
        });
        try {
          cell = { width: probe.width(), height: probe.height() };
          if (cell.width <= 0 || cell.height <= 0)
            throw new Error("Renderer font has invalid cell dimensions");
          cellMetricsRef.current.set(metricKey, cell);
        } finally {
          probe.delete();
        }
      }
      const requested = requestedGridRef.current;
      const estimatedCols =
        requested.columns > 0
          ? Math.min(400, Math.max(20, Math.floor(requested.columns)))
          : Math.max(20, Math.floor(containerWidth / cell.width));
      const estimatedRows =
        requested.rows > 0
          ? Math.min(150, Math.max(10, Math.floor(requested.rows)))
          : Math.max(
              10,
              Math.floor(Math.min(containerHeight, 2000) / cell.height),
            );
      if (estimatedCols > 400 || estimatedRows > 150)
        throw new Error(
          `Grid dimensions exceed limit: ${estimatedCols}x${estimatedRows}`,
        );
      const configPtr: RendererConfig = {
        cols: estimatedCols,
        rows: estimatedRows,
        matrix: isMatrixMode,
      };
      return { configPtr, estimatedCols, estimatedRows };
    },
    [callRendererCreate],
  );

  const getRendererDimensions = useCallback(() => {
    const renderer = rendererPtrRef.current;
    if (!renderer) throw new Error("WASM renderer is unavailable");
    return {
      cols: renderer.cols(),
      rows: renderer.rows(),
      pixelWidth: renderer.width(),
      pixelHeight: renderer.height(),
    };
  }, []);
  // Handle container resize by recreating renderer and updating canvas
  const handleContainerResize = useCallback(
    (newWidth: number, newHeight: number, canvas: HTMLCanvasElement) => {
      if (
        !moduleRef.current ||
        !rendererPtrRef.current ||
        newWidth <= 0 ||
        newHeight <= 0
      ) {
        return;
      }

      // Cap container height to prevent feedback loops where expanding canvas
      // causes ResizeObserver to report larger container, creating even larger renderer
      // Use viewport height as the hard limit - content should fit in visible area
      const cappedHeight = Math.min(newHeight, window.innerHeight);
      const oldRendererPtr = rendererPtrRef.current;
      if (cappedHeight < newHeight) {
        console.debug("[handleContainerResize] Capping height to viewport:", {
          originalHeight: newHeight,
          cappedHeight,
          viewportHeight: window.innerHeight,
        });
      }

      try {
        // Skip reinitialization during rapid resizes to avoid "Too many string options" errors.
        // Only reinitialize once per resize debounce cycle to preserve WASM options state.
        // The new renderer will use whatever settings were last applied.

        // Create new renderer with updated dimensions using the actual WASM matrix rain state
        const isMatrixMode = currentMatrixModeRef.current;
        console.debug("[handleContainerResize] STARTING RECREATION", {
          newWidth,
          newHeight,
          isMatrixMode,
        });
        const { configPtr, estimatedCols, estimatedRows } = createConfigStruct(
          newWidth,
          cappedHeight,
          isMatrixMode,
        );
        console.debug("[handleContainerResize] createConfigStruct returned", {
          configPtr,
          estimatedCols,
          estimatedRows,
        });
        console.debug(`[handleContainerResize] CALLING callRendererCreate...`);
        const rendererPtr = callRendererCreate(configPtr);
        rendererPtrRef.current = rendererPtr;

        console.debug("[handleContainerResize] CALLING getRendererDimensions");
        let dims: ReturnType<typeof getRendererDimensions>;
        try {
          dims = getRendererDimensions();
          if (dims.pixelWidth <= 0 || dims.pixelHeight <= 0) {
            throw new Error(
              "replacement renderer has invalid pixel dimensions",
            );
          }
        } catch (error) {
          rendererPtrRef.current = oldRendererPtr;
          rendererPtr.delete();
          throw error;
        }

        if (dims.pixelHeight === 0) {
          console.error("[handleContainerResize] ERROR: pixelHeight=0!", dims);
        }

        console.debug("[handleContainerResize] SETTING canvas dimensions", {
          width: dims.pixelWidth,
          height: dims.pixelHeight,
        });
        canvas.width = dims.pixelWidth;
        canvas.height = dims.pixelHeight;

        // Keep the current renderer usable until its replacement is complete.
        oldRendererPtr.delete();

        // Debounce dimension updates to batch multiple resize events
        // Prevents render loop from restarting on every ResizeObserver fire
        pendingDimensionsRef.current = { cols: dims.cols, rows: dims.rows };
        if (resizeTimeoutRef.current) {
          clearTimeout(resizeTimeoutRef.current);
        }
        resizeTimeoutRef.current = setTimeout(() => {
          if (pendingDimensionsRef.current) {
            updateDimensionsRef.current?.(
              pendingDimensionsRef.current.cols,
              pendingDimensionsRef.current.rows,
            );
            pendingDimensionsRef.current = null;
          }
          resizeTimeoutRef.current = null;
        }, 300);
      } catch (err) {
        // Attempt recovery without reinitialization
        console.error("[AsciiRenderer] FATAL: Failed to resize renderer:", err);
        if (err instanceof Error) {
          console.error("[AsciiRenderer] Error message:", err.message);
          console.error("[AsciiRenderer] Stack:", err.stack);

          // If dimension validation failed, try to recover with more conservative dimensions
          if (
            err.message.includes("dimension") ||
            err.message.includes("limit")
          ) {
            console.warn(
              "[AsciiRenderer] Dimension validation failed, attempting recovery with smaller grid",
            );
            try {
              // Retry with a capped grid size to avoid cascading allocation
              const recoveryWidth = Math.min(newWidth, 1280);
              const recoveryHeight = Math.min(cappedHeight, 2000); // Use same max as createConfigStruct
              const recoveryIsMatrixMode = getMatrixRain();
              const recoveryPixelsPerRow = recoveryIsMatrixMode ? 32 : 20;
              const recoveryCols = Math.max(1, Math.floor(recoveryWidth / 10));
              const recoveryRows = Math.max(
                1,
                Math.floor(recoveryHeight / recoveryPixelsPerRow),
              );

              console.debug(
                "[AsciiRenderer] Recovery attempt with capped dimensions:",
                {
                  recoveryCols,
                  recoveryRows,
                  originalWidth: newWidth,
                  originalHeight: newHeight,
                  recoveryWidth,
                  recoveryHeight,
                  recoveryIsMatrixMode,
                },
              );

              const { configPtr: retryConfigPtr } = createConfigStruct(
                recoveryWidth,
                recoveryHeight,
                recoveryIsMatrixMode,
              );
              const retryRendererPtr = callRendererCreate(retryConfigPtr);
              rendererPtrRef.current = retryRendererPtr;
              const retryDims = getRendererDimensions();
              if (retryDims.pixelWidth <= 0 || retryDims.pixelHeight <= 0) {
                rendererPtrRef.current = oldRendererPtr;
                retryRendererPtr.delete();
                throw new Error(
                  "recovery renderer has invalid pixel dimensions",
                  { cause: err },
                );
              }
              canvas.width = retryDims.pixelWidth;
              canvas.height = retryDims.pixelHeight;
              oldRendererPtr.delete();
              if (resizeTimeoutRef.current) {
                clearTimeout(resizeTimeoutRef.current);
                resizeTimeoutRef.current = null;
              }
              pendingDimensionsRef.current = null;
              updateDimensionsRef.current?.(retryDims.cols, retryDims.rows);
              console.debug(
                "[AsciiRenderer] Recovery successful with dimensions:",
                retryDims,
              );
            } catch (recoveryErr) {
              console.error(
                "[AsciiRenderer] Recovery also failed:",
                recoveryErr,
              );
            }
          }
        }
      }
    },
    [createConfigStruct, callRendererCreate, getRendererDimensions],
  );

  // Set up ResizeObserver on container
  const setupResizeObserver = useCallback(
    (container: HTMLElement, canvas: HTMLCanvasElement) => {
      resizeObserverRef.current = new ResizeObserver((entries) => {
        const entry = entries[0];
        if (!entry) return;

        const newWidth = Math.round(entry.contentRect.width);
        const newHeight = Math.round(entry.contentRect.height);

        if (
          observedContainerSizeRef.current.width === newWidth &&
          observedContainerSizeRef.current.height === newHeight
        ) {
          return;
        }
        observedContainerSizeRef.current = {
          width: newWidth,
          height: newHeight,
        };

        console.debug("[ResizeObserver] size change", {
          newWidth,
          newHeight,
          canvasCurrentWidth: canvas.width,
          canvasCurrentHeight: canvas.height,
        });

        handleContainerResize(newWidth, newHeight, canvas);
      });

      resizeObserverRef.current.observe(container);
    },
    [handleContainerResize],
  );

  // Initialize WASM renderer and set up observation
  const initializeRenderer = useCallback(
    (canvas: HTMLCanvasElement) => {
      if (!moduleRef.current) {
        throw new Error("moduleRef.current is null during initializeRenderer");
      }

      moduleRef.current.canvas = canvas;

      // A shared client WASM instance already has initialized options. Running
      // mirror_init_with_args on it would initialize the same C option state a
      // second time and switch the active mode underneath the client.
      if (initializeOptions) reinitializeWithCurrentSettings();

      const container = canvas.parentElement;
      const containerRect = container?.getBoundingClientRect();
      const containerWidth = Math.round(containerRect?.width ?? 1200);
      const containerHeight = Math.round(containerRect?.height ?? 1000);
      observedContainerSizeRef.current = {
        width: containerWidth,
        height: containerHeight,
      };

      // Create config and renderer, checking current matrix mode
      const isMatrixMode = matrixMode;
      currentMatrixModeRef.current = isMatrixMode;
      const { configPtr } = createConfigStruct(
        containerWidth,
        containerHeight,
        isMatrixMode,
      );
      const rendererPtr = callRendererCreate(configPtr);
      rendererPtrRef.current = rendererPtr;

      // Get actual dimensions and set canvas size
      const dims = getRendererDimensions();
      canvas.width = dims.pixelWidth;
      canvas.height = dims.pixelHeight;

      // Notify parent of initial dimensions
      updateDimensionsRef.current?.(dims.cols, dims.rows);
      setupDoneRef.current = true;

      // Set up resize observation
      if (container) {
        setupResizeObserver(container, canvas);
      }
    },
    [
      createConfigStruct,
      callRendererCreate,
      getRendererDimensions,
      setupResizeObserver,
      reinitializeWithCurrentSettings,
      initializeOptions,
      matrixMode,
    ],
  );

  useEffect(() => {
    if (!canvasRef.current) {
      return;
    }

    const canvas = canvasRef.current;

    if (setupDoneRef.current) {
      return;
    }

    // Try to initialize synchronously
    let timeoutId: ReturnType<typeof setTimeout> | undefined;

    try {
      if (moduleRef.current) {
        initializeRenderer(canvas);
      } else {
        // Module not ready yet - wait one microtask for it to load, then retry
        timeoutId = setTimeout(() => {
          if (moduleRef.current && !setupDoneRef.current) {
            initializeRenderer(canvas);
          }
        }, 0);
      }
    } catch (err) {
      console.error("[AsciiRenderer] Initialization failed:", err);
      setupDoneRef.current = false;
    }

    return () => {
      if (timeoutId !== undefined) {
        clearTimeout(timeoutId);
      }
      if (resizeTimeoutRef.current) {
        clearTimeout(resizeTimeoutRef.current);
        resizeTimeoutRef.current = null;
      }
    };
  }, [wasmModuleReady, canvasRef, initializeRenderer]);

  useEffect(
    () => () => {
      resizeObserverRef.current?.disconnect();
      resizeObserverRef.current = null;
      rendererPtrRef.current?.delete();
      rendererPtrRef.current = null;
      setupDoneRef.current = false;
    },
    [],
  );

  const triggerRendererRecreate = useCallback(() => {
    if (!moduleRef.current || !canvasRef.current || !setupDoneRef.current) {
      return;
    }
    const canvas = canvasRef.current;
    const container = canvas.parentElement;
    if (container) {
      const rect = container.getBoundingClientRect();
      // Cap dimensions to prevent cascading growth when toggling matrix mode
      // Use viewport dimensions as hard limit - canvas should never exceed visible area
      const maxWidth = Math.min(rect.width, window.innerWidth);
      const maxHeight = Math.min(rect.height, window.innerHeight);
      handleContainerResize(
        Math.round(maxWidth),
        Math.round(maxHeight),
        canvas,
      );
    }
  }, [handleContainerResize, canvasRef]);

  // Recreate the grid when its font or requested dimensions change.
  useEffect(() => {
    if (setupDoneRef.current && canvasRef.current) {
      triggerRendererRecreate();
    }
  }, [matrixMode, columns, rows, triggerRendererRecreate, canvasRef]);

  return {
    moduleRef,
    setupDoneRef,
    rendererPtrRef,
    resizeTimeoutRef,
    setUpdateDimensions: (fn: (cols: number, rows: number) => void) => {
      updateDimensionsRef.current = fn;
    },
    triggerRendererRecreate,
  };
}
