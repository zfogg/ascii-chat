/**
 * AsciiRenderer - WASM-based terminal video rendering component
 *
 * Architecture:
 * This component is a thin orchestrator for two specialized hooks that handle rendering initialization
 * and imperative handle methods. State is distributed across the hooks based on concern:
 *
 * Component Responsibilities:
 * - Own DOM-related refs: canvasRef (the HTML canvas element)
 * - Track WASM module readiness: previousWasmReadyRef (to detect readiness transitions)
 * - Orchestrate hook initialization in dependency order
 * - Pass resizeTimeoutRef between hooks (synchronization point for debounce)
 *
 * useInitAsciiRenderer Hook:
 * - Handles WASM module loading and renderer creation (C++ termrenderer struct)
 * - Sets up canvas dimensions based on renderer's pixel output
 * - Manages ResizeObserver for responsive canvas sizing
 * - Owns debounce timeout (resizeTimeoutRef) to prevent mid-resize frame rendering
 * - Owns: moduleRef, setupDoneRef, rendererPtrRef, resizeTimeoutRef, updateDimensionsRef, resizeObserverRef, pendingDimensionsRef
 * - Returns: moduleRef, setupDoneRef, rendererPtrRef, resizeTimeoutRef, setUpdateDimensions callback
 * - Input: wasmModuleReady flag
 *
 * useAsciiRendererHandle Hook:
 * - Implements imperative methods exposed via forwardRef: writeFrame(), getDimensions(), clear()
 * - writeFrame() handles WASM memory allocation, frame rendering, and framebuffer display
 * - Monitors resizeTimeoutRef to skip rendering during resize debounce (prevents dimension mismatches)
 * - Tracks FPS and calls onFpsChange callback
 * - Owns: frameCountRef, fpsDisplayRef, dimensionsRef, fpsUpdateTimeRef, firstRenderDoneRef
 * - Returns: updateDimensions callback and fpsDisplayRef
 * - Input: moduleRef, setupDoneRef, rendererPtrRef, canvasRef, resizeTimeoutRef
 *
 * Data Flow:
 * 1. Component receives wasmModuleReady prop → passes to useInitAsciiRenderer
 * 2. useInitAsciiRenderer initializes WASM renderer, sets up ResizeObserver
 * 3. useInitAsciiRenderer calls setUpdateDimensions(callback) to register dimension change handler
 * 4. Parent calls imperative handle methods (writeFrame) → useAsciiRendererHandle receives frames
 * 5. useAsciiRendererHandle uses resizeTimeoutRef (from initialization hook) to skip rendering during resize
 * 6. ResizeObserver fires → updates debounce timeout, batches dimension change callbacks
 *
 * Synchronization Points:
 * - resizeTimeoutRef: Shared between initialization hook (sets timeout) and handle hook (checks it)
 *   prevents rendering frames with mismatched dimensions during resize
 * - setUpdateDimensions callback: Passes handle hook's updateDimensions to init hook
 *   allows init hook to notify parent when grid dimensions change
 */

import { forwardRef, type RefObject, useRef } from "react";
import type { AsciiRendererHandle, AsciiRendererProps } from "./types";
import { useInitAsciiRenderer } from "./useInitAsciiRenderer";
import { useAsciiRendererHandle } from "./useAsciiRendererHandle";

const AsciiRenderer = forwardRef<AsciiRendererHandle, AsciiRendererProps>(
  function AsciiRenderer(
    {
      onDimensionsChange,
      onFpsChange,
      error,
      showFps = true,
      connectionState,
      wasmModuleReady,
      matrixMode = false,
    },
    ref,
  ) {
    const canvasRef = useRef<HTMLCanvasElement>(null);

    const previousWasmReadyRef = useRef<boolean | undefined>(undefined);
    if (previousWasmReadyRef.current !== wasmModuleReady) {
      previousWasmReadyRef.current = wasmModuleReady;
    }

    // Initialize renderer and get refs
    const {
      moduleRef,
      setupDoneRef,
      rendererPtrRef,
      resizeTimeoutRef,
      setUpdateDimensions,
      triggerRendererRecreate,
    } = useInitAsciiRenderer({
      canvasRef: canvasRef as RefObject<HTMLCanvasElement | null>,
      wasmModuleReady,
      matrixMode,
    });

    // Set up imperative handle and get updateDimensions + fpsDisplayRef
    const { updateDimensions, fpsDisplayRef } = useAsciiRendererHandle({
      ref,
      moduleRef,
      setupDoneRef,
      rendererPtrRef,
      canvasRef: canvasRef as RefObject<HTMLCanvasElement | null>,
      resizeTimeoutRef,
      showFps,
      onFpsChange,
      onDimensionsChange,
      onRecreateRenderer: triggerRendererRecreate,
    });

    // Provide updateDimensions to the initialization hook
    setUpdateDimensions(updateDimensions);

    return (
      <div className="ascii-canvas-container w-full h-full flex flex-col items-center justify-center overflow-hidden relative flex-1">
        <style>
          {`
          canvas {
            display: block;
            image-rendering: pixelated;
            image-rendering: crisp-edges;
          }
        `}
        </style>
        <canvas ref={canvasRef} className="ascii-canvas" />
        {connectionState === 0 && (
          <div className="absolute inset-0 flex items-center justify-center bg-black/40 rounded pointer-events-none">
            <div className="text-5xl font-bold text-red-500 drop-shadow-lg">
              DISCONNECTED
            </div>
          </div>
        )}

        {/* FPS counter - hidden, displayed in control bar instead */}
        {showFps && (
          <div ref={fpsDisplayRef} style={{ display: "none" }}>
            --
          </div>
        )}

        {/* Error bar */}
        {error && (
          <div className="px-4 pb-2">
            <div className="p-4 bg-terminal-1 text-terminal-fg rounded">
              {error}
            </div>
          </div>
        )}
      </div>
    );
  },
);

export { AsciiRenderer };
