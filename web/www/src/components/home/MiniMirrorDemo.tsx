import { useCallback, useEffect, useRef, useState } from "react";
import {
  initMirrorWasm,
  cleanupMirrorWasm,
  convertFrameToAscii,
  isWasmReady,
  setDimensions,
} from "@ascii-chat/shared/wasm";
import type { EmscriptenModuleFactory } from "@ascii-chat/shared/wasm";
import { useCanvasCapture } from "@ascii-chat/shared/hooks";
import {
  AsciiRenderer,
  type AsciiRendererHandle,
} from "@ascii-chat/shared/components";
import { SITES } from "@ascii-chat/shared/utils";

type DemoSource = "webcam" | "demo" | null;

export default function MiniMirrorDemo() {
  const [source, setSource] = useState<DemoSource>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [wasmReady, setWasmReady] = useState(false);
  const [termDims, setTermDims] = useState({ cols: 0, rows: 0 });

  const rendererRef = useRef<AsciiRendererHandle>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const frameIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const { captureFrame } = useCanvasCapture(videoRef, canvasRef);

  const initWasm = useCallback(async () => {
    if (isWasmReady()) {
      setWasmReady(true);
      return;
    }

    // Dynamic import of the Emscripten factory (bundled via Vite alias)
    // @ts-expect-error - Generated file without types
    const { default: factory } = await import("mirror-wasm-factory");

    const wasmBaseUrl = SITES.WEB.replace(/:443$/, "");
    await initMirrorWasm(factory as EmscriptenModuleFactory, {
      locateFile: (path: string) => `${wasmBaseUrl}/wasm/${path}`,
    });

    setWasmReady(true);
  }, []);

  const startWebcam = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      await initWasm();

      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: 640 },
          height: { ideal: 480 },
          facingMode: "user",
        },
        audio: false,
      });

      streamRef.current = stream;
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        await new Promise<void>((resolve) => {
          videoRef.current!.addEventListener(
            "loadedmetadata",
            () => {
              if (canvasRef.current) {
                canvasRef.current.width = 640;
                canvasRef.current.height = 480;
              }
              resolve();
            },
            { once: true },
          );
        });
      }

      setSource("webcam");
    } catch (err) {
      setError(
        `Camera access failed: ${err instanceof Error ? err.message : String(err)}`,
      );
    } finally {
      setLoading(false);
    }
  }, [initWasm]);

  const startDemo = useCallback(async () => {
    setLoading(true);
    setError(null);
    try {
      await initWasm();

      if (videoRef.current) {
        videoRef.current.src = "/assets/demo-video.mp4";
        videoRef.current.loop = true;
        videoRef.current.muted = true;
        videoRef.current.playsInline = true;
        await new Promise<void>((resolve, reject) => {
          videoRef.current!.addEventListener(
            "loadedmetadata",
            () => {
              if (canvasRef.current) {
                canvasRef.current.width = videoRef.current!.videoWidth;
                canvasRef.current.height = videoRef.current!.videoHeight;
              }
              resolve();
            },
            { once: true },
          );
          videoRef.current!.addEventListener(
            "error",
            () => {
              reject(new Error("Failed to load demo video"));
            },
            { once: true },
          );
        });
        await videoRef.current.play();
      }

      setSource("demo");
    } catch (err) {
      setError(
        `Demo failed: ${err instanceof Error ? err.message : String(err)}`,
      );
    } finally {
      setLoading(false);
    }
  }, [initWasm]);

  const stop = useCallback(() => {
    if (frameIntervalRef.current) {
      clearInterval(frameIntervalRef.current);
      frameIntervalRef.current = null;
    }
    if (streamRef.current) {
      streamRef.current.getTracks().forEach((t) => t.stop());
      streamRef.current = null;
    }
    if (videoRef.current) {
      videoRef.current.pause();
      videoRef.current.srcObject = null;
      videoRef.current.src = "";
    }
    rendererRef.current?.clear();
    setSource(null);
  }, []);

  // Sync dimensions to WASM when they change (matches Mirror.tsx pattern)
  useEffect(() => {
    if (wasmReady && termDims.cols > 0 && termDims.rows > 0) {
      try {
        setDimensions(termDims.cols, termDims.rows);
      } catch (err) {
        console.error("[MiniMirrorDemo] Failed to sync dimensions:", err);
      }
    }
  }, [termDims, wasmReady]);

  // Render loop (matches useMirrorRenderLoop pattern)
  useEffect(() => {
    if (!source || !wasmReady) return;
    if (termDims.cols <= 0 || termDims.rows <= 0) return;

    const interval = setInterval(() => {
      if (!isWasmReady() || !rendererRef.current) return;

      const frame = captureFrame();
      if (!frame) return;

      const expectedSize = frame.width * frame.height * 4;
      if (frame.data.length !== expectedSize) return;

      try {
        const ascii = convertFrameToAscii(
          frame.data,
          frame.width,
          frame.height,
        );
        if (ascii) {
          rendererRef.current.writeFrame(ascii);
        }
      } catch (err) {
        console.error("[MiniMirrorDemo] Render error:", err);
      }
    }, 1000 / 24);

    frameIntervalRef.current = interval;
    return () => clearInterval(interval);
  }, [source, wasmReady, termDims, captureFrame]);

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      stop();
      cleanupMirrorWasm();
    };
  }, [stop]);

  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => setTermDims(dims),
    [],
  );

  return (
    <section className="mb-12 sm:mb-16">
      {/* Hidden video and canvas for frame capture (matches PageLayout pattern) */}
      <div
        style={{
          position: "fixed",
          bottom: 0,
          right: 0,
          width: "1px",
          height: "1px",
          overflow: "hidden",
          pointerEvents: "none",
        }}
      >
        <video
          ref={videoRef}
          autoPlay
          muted
          playsInline
          style={{ width: "640px", height: "480px" }}
        />
        <canvas ref={canvasRef} />
      </div>

      <div
        className="relative rounded-lg border border-gray-700 bg-[#0c0c0c] overflow-hidden flex flex-col"
        style={{ height: "33vh", minHeight: "200px" }}
      >
        {/* AsciiRenderer always mounted (matches Mirror page pattern) */}
        <AsciiRenderer
          ref={rendererRef}
          onDimensionsChange={handleDimensionsChange}
          showFps={false}
        />

        {/* Overlay: buttons before start, stop button after */}
        {!source ? (
          <div className="absolute inset-0 flex flex-col items-center justify-center gap-4 z-10">
            <p className="text-gray-400 text-sm sm:text-base">
              Try ascii-chat in your browser
            </p>
            <div className="flex gap-3">
              <button
                onClick={startWebcam}
                disabled={loading}
                className="px-4 py-2 rounded bg-cyan-600 hover:bg-cyan-500 text-white text-sm font-medium transition-colors disabled:opacity-50"
              >
                {loading ? "Loading..." : "Webcam"}
              </button>
              <button
                onClick={startDemo}
                disabled={loading}
                className="px-4 py-2 rounded bg-purple-600 hover:bg-purple-500 text-white text-sm font-medium transition-colors disabled:opacity-50"
              >
                {loading ? "Loading..." : "Demo Video"}
              </button>
            </div>
            {error && <p className="text-red-400 text-xs mt-2">{error}</p>}
          </div>
        ) : (
          <button
            onClick={stop}
            className="absolute top-2 right-2 px-3 py-1 rounded bg-gray-800/80 hover:bg-gray-700 text-gray-300 text-xs font-medium transition-colors z-10"
          >
            Stop
          </button>
        )}
      </div>
    </section>
  );
}
