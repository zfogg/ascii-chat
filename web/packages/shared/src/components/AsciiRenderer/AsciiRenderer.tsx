import { forwardRef, useEffect, useRef, type RefObject } from "react";
import { getMirrorModule, type MirrorModule } from "../../wasm/mirror";
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
    },
    ref,
  ) {
    console.log(
      "[AsciiRenderer] Render with wasmModuleReady=" + wasmModuleReady,
    );
    const previousWasmReadyRef = useRef<boolean | undefined>(undefined);
    if (previousWasmReadyRef.current !== wasmModuleReady) {
      previousWasmReadyRef.current = wasmModuleReady;
    }
    const canvasRef = useRef<HTMLCanvasElement>(null);
    const moduleRef = useRef<MirrorModule | null>(null);
    const rendererPtrRef = useRef<number>(0);
    const setupDoneRef = useRef(false);
    const pendingDimensionsRef = useRef<{ cols: number; rows: number } | null>(
      null,
    );
    const resizeTimeoutRef = useRef<ReturnType<typeof setTimeout> | null>(null);
    const resizeObserverRef = useRef<ResizeObserver | null>(null);

    // FPS tracking
    const frameCountRef = useRef(0);
    const fpsUpdateTimeRef = useRef<number | null>(null);
    const fpsDisplayRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
      fpsUpdateTimeRef.current = performance.now();
    }, []);

    // NOTE: Canvas sizing is handled by the renderer initialization code
    // We set canvas to the renderer's actual pixel output dimensions, not the container size

    useEffect(() => {
      if (wasmModuleReady) {
        const module = getMirrorModule();
        moduleRef.current = module;
      }
    }, [wasmModuleReady]);

    // Set up imperative handle and get updateDimensions callback
    const updateDimensions = useAsciiRendererHandle({
      ref,
      moduleRef,
      setupDoneRef,
      rendererPtrRef,
      canvasRef: canvasRef as RefObject<HTMLCanvasElement | null>,
      resizeTimeoutRef,
      showFps,
      onFpsChange,
      onDimensionsChange,
      fpsDisplayRef: fpsDisplayRef as RefObject<HTMLDivElement | null>,
      fpsUpdateTimeRef,
      frameCountRef,
    });

    // Use the initialization hook
    useInitAsciiRenderer({
      canvasRef: canvasRef as RefObject<HTMLCanvasElement | null>,
      moduleRef,
      rendererPtrRef,
      setupDoneRef,
      resizeObserverRef,
      resizeTimeoutRef,
      pendingDimensionsRef,
      updateDimensions,
      wasmModuleReady,
    });

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
