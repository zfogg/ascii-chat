import { useCallback, useEffect, useRef, useState } from "react";
import {
  initMirrorWasm,
  cleanupMirrorWasm,
  convertFrameToAscii,
  isWasmReady,
  setDimensions,
  setFlipX,
  setFlipY,
  setColorMode,
  setColorFilter,
  setRenderMode,
  setPalette,
  setPaletteChars,
  setMatrixRain,
  setTargetFps,
  ColorMode,
  ColorFilter,
  RenderMode,
} from "@ascii-chat/shared/wasm";
import type { EmscriptenModuleFactory } from "@ascii-chat/shared/wasm";
import { useCanvasCapture } from "@ascii-chat/shared/hooks";
import {
  Heading,
  AsciiRenderer,
  type AsciiRendererHandle,
} from "@ascii-chat/shared/components";
import { MediaSourceType, type MediaSource } from "@ascii-chat/shared/utils";
import { registerActiveDemo, unregisterActiveDemo } from "./activeDemo";
import type { DemoOption } from "./types";

interface MirrorDemoWidgetProps {
  demoOptions?: DemoOption[];
  defaultOptionIndex?: number;
  height?: string;
  minHeight?: number;
  showHeader?: boolean;
}

let lastMatrixRain = false;

function applyDemoOption(
  option: DemoOption,
  sourceFlipX: boolean,
  onMatrixRainChange?: (matrixMode: boolean) => void,
  onSetMatrixMode?: (mode: boolean) => void,
): void {
  const s = option.settings;
  // Reset all options to defaults first, then apply preset overrides.
  // This prevents stale state from a previous demo leaking through.
  setColorMode(s.colorMode ?? ColorMode.AUTO);
  setColorFilter(s.colorFilter ?? ColorFilter.NONE);
  setRenderMode(s.renderMode ?? RenderMode.FOREGROUND);
  setPalette(s.palette ?? "standard");
  const matrixRainNow = s.matrixRain ?? false;
  console.log("[applyDemoOption]", {
    option: option.label,
    matrixRainNow,
    lastMatrixRain,
  });
  // Set matrix flag first so C side knows about it before renderer recreation
  setMatrixRain(matrixRainNow);
  // Update matrix mode state for renderer prop
  onSetMatrixMode?.(matrixRainNow);
  // Trigger renderer recreation if matrix rain changed (after C side is updated)
  if (matrixRainNow !== lastMatrixRain) {
    lastMatrixRain = matrixRainNow;
    console.log("[applyDemoOption] matrix mode changed, triggering recreate");
    onMatrixRainChange?.(matrixRainNow);
  }
  setFlipX(s.flipX !== undefined ? s.flipX : sourceFlipX);
  setFlipY(s.flipY ?? false);
  if (s.paletteChars !== undefined) {
    console.log(
      "[PALETTE] Setting paletteChars:",
      Array.from(s.paletteChars).map(
        (c, i) =>
          `${i}: U+${c.charCodeAt(0).toString(16).padStart(4, "0")} "${c}"`,
      ),
    );
    setPaletteChars(s.paletteChars);
  }
  if (s.targetFps !== undefined) setTargetFps(s.targetFps);
}

export default function MirrorDemoWidget({
  demoOptions,
  defaultOptionIndex = 0,
  height = "36vh",
  minHeight = 200,
  showHeader = true,
}: MirrorDemoWidgetProps) {
  const [source, setSource] = useState<MediaSource>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [wasmReady, setWasmReady] = useState(false);
  const [muted, setMuted] = useState(false);
  const [paused, setPaused] = useState(false);
  const [termDims, setTermDims] = useState({ cols: 0, rows: 0 });
  const [debugLogs, setDebugLogs] = useState<string[]>([]);
  const [selectedOptionId, setSelectedOptionId] = useState<string | null>(
    demoOptions?.[defaultOptionIndex]?.id ?? null,
  );
  const [matrixMode, setMatrixMode] = useState(false);

  const rendererRef = useRef<AsciiRendererHandle>(null);
  const videoRef = useRef<HTMLVideoElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const frameIntervalRef = useRef<ReturnType<typeof setInterval> | null>(null);
  const sourceRef = useRef<MediaSource>(null);
  const frameCountRef = useRef(0);

  const { captureFrame } = useCanvasCapture(videoRef, canvasRef);

  const selectedOption =
    demoOptions?.find((o) => o.id === selectedOptionId) ?? null;

  const addDebugLog = useCallback((msg: string) => {
    const timestamp = new Date().toLocaleTimeString();
    const logMsg = `[${timestamp}] ${msg}`;
    console.log(logMsg);
    setDebugLogs((prev) => [...prev, logMsg].slice(-15));
  }, []);

  const initWasm = useCallback(async () => {
    if (isWasmReady()) {
      setWasmReady(true);
      return;
    }

    addDebugLog("Loading WASM factory...");
    // @ts-expect-error - Generated file without types
    const { default: factory } = await import("mirror-wasm-factory");

    // Use window.location.origin to load WASM from current site
    // This works whether running on localhost, manjaro-twopal, or production
    const wasmBaseUrl = window.location.origin;
    addDebugLog(`Loading WASM from ${wasmBaseUrl}/wasm/`);

    // Build initial args from the first demo option's settings
    const initialArgs: string[] = [];
    const firstOption = demoOptions?.[defaultOptionIndex];
    if (firstOption?.settings?.matrixRain) {
      initialArgs.push("--matrix");
    }

    await initMirrorWasm(factory as EmscriptenModuleFactory, {
      locateFile: (path: string) => `${wasmBaseUrl}/wasm/${path}`,
      initialArgs,
    });

    setWasmReady(true);
  }, [addDebugLog, demoOptions, defaultOptionIndex]);

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
    sourceRef.current = null;
    setMuted(false);
    setPaused(false);
  }, []);

  const applySelectedOption = useCallback(
    (sourceFlipX: boolean) => {
      if (selectedOption && isWasmReady()) {
        applyDemoOption(
          selectedOption,
          sourceFlipX,
          () => {
            rendererRef.current?.recreateRenderer();
          },
          setMatrixMode,
        );
      }
    },
    [selectedOption],
  );

  const startWebcam = useCallback(async () => {
    rendererRef.current?.clear();
    setLoading(true);
    setError(null);
    try {
      registerActiveDemo(stop);
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

      const sourceFlipX = true;
      setFlipX(sourceFlipX);
      setFlipY(false);
      applySelectedOption(sourceFlipX);

      if (termDims.cols > 0 && termDims.rows > 0) {
        setDimensions(termDims.cols, termDims.rows);
      }

      sourceRef.current = MediaSourceType.WEBCAM;
      setSource(MediaSourceType.WEBCAM);
    } catch (err) {
      setError(
        `Camera access failed: ${err instanceof Error ? err.message : String(err)}`,
      );
    } finally {
      setLoading(false);
    }
  }, [initWasm, stop, applySelectedOption, termDims]);

  const startDemo = useCallback(async () => {
    rendererRef.current?.clear();
    setLoading(true);
    setError(null);
    setDebugLogs([]);
    addDebugLog("Starting demo...");
    try {
      registerActiveDemo(stop);
      addDebugLog("Initializing WASM...");
      await initWasm();
      addDebugLog("WASM initialized");

      if (videoRef.current) {
        addDebugLog("Setting up video element");
        videoRef.current.src = "/assets/demo-video-sun-models.webm";
        // Don't set crossOrigin for same-domain resources
        // (production server doesn't provide CORS headers)
        videoRef.current.loop = true;
        videoRef.current.muted = true;
        videoRef.current.playsInline = true;

        await new Promise<void>((resolve, reject) => {
          const loadedMetadataHandler = () => {
            addDebugLog(
              `Video metadata loaded: ${videoRef.current!.videoWidth}x${videoRef.current!.videoHeight}`,
            );
            if (canvasRef.current) {
              // Set canvas to video dimensions to preserve aspect ratio
              canvasRef.current.width = videoRef.current!.videoWidth;
              canvasRef.current.height = videoRef.current!.videoHeight;
            }
            resolve();
          };

          const errorHandler = () => {
            const video = videoRef.current!;
            const errorCode = video.error?.code;
            const errorMsg = video.error?.message || "Unknown error";
            const videoSrc = video.src;
            const errorText = `Video error code ${errorCode}: ${errorMsg} (src: ${videoSrc})`;
            addDebugLog(errorText);
            // Always log to console for production debugging
            console.error("[MirrorDemoWidget]", errorText);
            reject(new Error(errorText));
          };

          addDebugLog("Waiting for video metadata...");
          videoRef.current!.addEventListener(
            "loadedmetadata",
            loadedMetadataHandler,
            {
              once: true,
            },
          );
          videoRef.current!.addEventListener("error", errorHandler, {
            once: true,
          });

          // Add timeout for debugging
          const timeout = setTimeout(() => {
            videoRef.current!.removeEventListener(
              "loadedmetadata",
              loadedMetadataHandler,
            );
            videoRef.current!.removeEventListener("error", errorHandler);
            addDebugLog("Video load timeout - metadata not received");
            reject(new Error("Video metadata load timeout"));
          }, 5000);

          // Store reference to clear timeout on success
          const originalResolve = resolve;
          resolve = () => {
            clearTimeout(timeout);
            originalResolve();
          };
        });

        addDebugLog("Playing video...");
        await videoRef.current.play();
        addDebugLog("Video playing, unmuting...");
        videoRef.current.muted = false;
      }

      const sourceFlipX = false;
      setFlipX(sourceFlipX);
      setFlipY(false);
      applySelectedOption(sourceFlipX);

      // Re-sync terminal dimensions now that the video is playing,
      // in case xterm measured dimensions before the layout settled
      if (termDims.cols > 0 && termDims.rows > 0) {
        setDimensions(termDims.cols, termDims.rows);
      }

      sourceRef.current = MediaSourceType.FILE;
      addDebugLog("Demo started successfully");
      setSource(MediaSourceType.FILE);
    } catch (err) {
      const errorMsg = err instanceof Error ? err.message : String(err);
      addDebugLog(`ERROR: ${errorMsg}`);
      setError(`Demo failed: ${errorMsg}`);
    } finally {
      setLoading(false);
    }
  }, [initWasm, stop, applySelectedOption, addDebugLog, termDims]);

  const togglePause = useCallback(() => {
    if (videoRef.current) {
      if (videoRef.current.paused) {
        void videoRef.current.play();
        setPaused(false);
      } else {
        videoRef.current.pause();
        setPaused(true);
      }
    }
  }, []);

  const restart = useCallback(() => {
    if (videoRef.current) {
      videoRef.current.currentTime = 0;
      void videoRef.current.play();
      setPaused(false);
    }
  }, []);

  const toggleMute = useCallback(() => {
    if (videoRef.current) {
      videoRef.current.muted = !videoRef.current.muted;
      setMuted(videoRef.current.muted);
    }
  }, []);

  const switchOption = useCallback(
    (option: DemoOption) => {
      setSelectedOptionId(option.id);
      if (isWasmReady() && source) {
        const sourceFlipX = source === MediaSourceType.WEBCAM;
        applyDemoOption(
          option,
          sourceFlipX,
          () => {
            rendererRef.current?.recreateRenderer();
          },
          setMatrixMode,
        );
      }
    },
    [source],
  );

  useEffect(() => {
    if (wasmReady && termDims.cols > 0 && termDims.rows > 0) {
      try {
        setDimensions(termDims.cols, termDims.rows);
      } catch (err) {
        console.error("[MirrorDemoWidget] Failed to sync dimensions:", err);
      }
    }
  }, [termDims, wasmReady]);

  useEffect(() => {
    addDebugLog(
      `Dimensions set: ${termDims.cols}x${termDims.rows}, wasmReady=${wasmReady}`,
    );
  }, [termDims, wasmReady, addDebugLog]);

  useEffect(() => {
    if (!source || !wasmReady) return;
    if (termDims.cols <= 0 || termDims.rows <= 0) return;

    const interval = setInterval(() => {
      if (
        !isWasmReady() ||
        !rendererRef.current ||
        !canvasRef.current ||
        !videoRef.current
      )
        return;

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
          frameCountRef.current++;

          // Log raw output for debugging UTF-8 rendering
          if (frameCountRef.current % 30 === 0) {
            // Log every 30 frames to avoid spam
            // Find a chunk with non-ASCII to inspect
            let sampleIdx = 0;
            for (let i = 0; i < Math.min(500, ascii.length); i++) {
              if (ascii.charCodeAt(i) > 127) {
                sampleIdx = i;
                break;
              }
            }

            const sample = ascii.substring(
              sampleIdx,
              Math.min(sampleIdx + 50, ascii.length),
            );
            const bytes = Array.from(sample)
              .map((c) => c.charCodeAt(0).toString(16).padStart(2, "0"))
              .join(" ");
            // eslint-disable-next-line no-control-regex
            const visible = sample.replace(/[\0-\u001f\u007f-\u009f]/gu, ".");

            console.log(`[UTF8 RENDER] Frame ${frameCountRef.current}:`);
            console.log(`  Sample text: "${visible}"`);
            console.log(`  Byte values: ${bytes}`);
            console.log(`  First 200 chars:`, ascii.substring(0, 200));
          }

          rendererRef.current.writeFrame(ascii);
        }
      } catch (err) {
        console.error("[MirrorDemoWidget] Render error:", err);
      }
    }, 1000 / 24);

    frameIntervalRef.current = interval;
    return () => clearInterval(interval);
  }, [source, wasmReady, termDims, captureFrame]);

  useEffect(() => {
    const stopFn = stop;
    return () => {
      stopFn();
      unregisterActiveDemo(stopFn);
      cleanupMirrorWasm();
    };
  }, [stop]);

  const handleDimensionsChange = useCallback(
    (dims: { cols: number; rows: number }) => setTermDims(dims),
    [],
  );

  const hasOptions = demoOptions && demoOptions.length > 0;

  return (
    <div className="mb-8">
      {showHeader && (
        <Heading level={3} className="heading-3 text-green-300 mb-3">
          🎮 Try it right now
        </Heading>
      )}
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
        className="relative bg-[#0c0c0c] overflow-hidden demo-widget-container"
        style={
          {
            "--demo-widget-height": height,
            "--demo-widget-min-height": `${minHeight}px`,
          } as unknown as React.CSSProperties
        }
      >
        <div className="demo-widget-inner">
          <AsciiRenderer
            ref={rendererRef}
            onDimensionsChange={handleDimensionsChange}
            showFps={false}
            wasmModuleReady={wasmReady}
            matrixMode={matrixMode}
          />
        </div>

        {/* Overlay container - explicitly sized to avoid iOS layout issues */}
        <div
          className="absolute pointer-events-none"
          style={{
            top: 0,
            left: 0,
            right: 0,
            bottom: 0,
            width: "100%",
            height: "100%",
          }}
        >
          {!source && (
            <div className="absolute inset-0 flex flex-col items-center justify-center gap-3 pointer-events-auto">
              <p className="text-gray-200 text-sm sm:text-base bg-gray-800 px-2 py-1 rounded">
                Select an option
              </p>
              {hasOptions && (
                <div className="flex flex-wrap gap-2 justify-center px-4 max-w-full">
                  {demoOptions.map((opt) => (
                    <button
                      key={opt.id}
                      onClick={() => switchOption(opt)}
                      className={`px-3 py-1 rounded text-xs font-medium transition-colors ${
                        selectedOptionId === opt.id
                          ? "bg-green-700 hover:bg-green-600 cursor-not-allowed text-white"
                          : "bg-gray-700 text-gray-300 hover:bg-gray-600 hover:scale-110 transform transition-transform cursor-pointer"
                      }`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              )}
              {hasOptions && selectedOption?.description && (
                <p className="text-gray-200 text-xs text-center px-2 py-1 bg-gray-800 rounded">
                  {selectedOption.description}
                </p>
              )}
              <div className="flex gap-3">
                <button
                  onClick={startWebcam}
                  disabled={loading}
                  className="px-4 py-2 rounded bg-cyan-600 hover:bg-cyan-500 hover:scale-110 transform transition-transform cursor-pointer text-white text-sm font-medium disabled:opacity-50"
                >
                  {loading ? "Loading..." : "Webcam"}
                </button>
                <button
                  onClick={startDemo}
                  disabled={loading}
                  className="px-4 py-2 rounded bg-purple-600 hover:bg-purple-500 hover:scale-110 transform transition-transform cursor-pointer text-white text-sm font-medium disabled:opacity-50"
                >
                  {loading ? "Loading..." : "Demo Video"}
                </button>
              </div>
              {error && <p className="text-red-400 text-xs mt-1">{error}</p>}
            </div>
          )}

          {source && (
            <>
              {source === MediaSourceType.FILE && (
                <div className="absolute bottom-2 right-2 flex gap-2 z-10 pointer-events-auto">
                  <button
                    onClick={togglePause}
                    className={`px-3 py-1 rounded text-xs font-medium transition-colors ${
                      paused
                        ? "bg-green-700/80 hover:bg-green-600 text-white"
                        : "bg-gray-800/80 hover:bg-gray-700 text-gray-300"
                    }`}
                  >
                    {paused ? "Play" : "Pause"}
                  </button>
                  <button
                    onClick={restart}
                    className="px-3 py-1 rounded text-xs font-medium transition-colors bg-gray-800/80 hover:bg-gray-700 text-gray-300"
                  >
                    Restart
                  </button>
                  <button
                    onClick={toggleMute}
                    className={`px-3 py-1 rounded text-xs font-medium transition-colors ${
                      muted
                        ? "bg-green-700/80 hover:bg-green-600 text-white"
                        : "bg-gray-800/80 hover:bg-gray-700 text-gray-300"
                    }`}
                  >
                    {muted ? "Unmute" : "Mute"}
                  </button>
                </div>
              )}
              {hasOptions && (
                <div className="absolute bottom-2 left-2 flex flex-col gap-1 z-10 pointer-events-auto">
                  {demoOptions.map((opt) => (
                    <button
                      key={opt.id}
                      onClick={() => switchOption(opt)}
                      className={`px-2 py-0.5 rounded text-xs font-medium transition-colors ${
                        selectedOptionId === opt.id
                          ? "bg-green-700/80 hover:bg-green-600 text-white"
                          : "bg-gray-800/80 text-gray-400 hover:bg-gray-700 hover:scale-110 transform transition-transform cursor-pointer"
                      }`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              )}
            </>
          )}
        </div>

        {/* Stop button - floats directly over canvas */}
        {source && (
          <button
            onClick={stop}
            className="absolute top-2 right-2 px-3 py-1 rounded bg-red-700/80 hover:bg-red-600 text-white text-xs font-medium transition-colors z-20"
          >
            Stop
          </button>
        )}
      </div>

      {/* Debug logs panel (dev only) */}
      {import.meta.env.MODE !== "production" && debugLogs.length > 0 && (
        <div className="mt-2 bg-gray-950 border border-gray-700 rounded p-2 max-h-32 overflow-y-auto">
          <p className="text-gray-500 text-xs font-medium mb-1">Debug Logs:</p>
          {debugLogs.map((log, i) => (
            <div
              key={i}
              className={`text-xs font-mono ${
                log.includes("ERROR") ? "text-red-400" : "text-green-400"
              }`}
            >
              {log}
            </div>
          ))}
        </div>
      )}

      <p className="text-gray-600 text-xs mt-2 sm:text-right">
        Demo video:{" "}
        <a
          href="https://www.youtube.com/watch?v=RtCaoKY769E"
          className="text-gray-500 hover:text-gray-400 underline"
          target="_blank"
          rel="noopener noreferrer"
        >
          Sun Models (ODESZA VIP Remix) - Live from Lollapalooza 2023
        </a>
      </p>
    </div>
  );
}
