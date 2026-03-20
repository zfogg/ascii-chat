import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type RefObject,
  type MutableRefObject,
} from "react";
import type { SettingsConfig } from "../components/Settings";
import { isWasmReady } from "../wasm/mirror";
import {
  setColorMode,
  setColorFilter,
  setPalette,
  setPaletteChars,
  setMatrixRain,
  setFlipX,
  isOptionsInitialized,
} from "../wasm/common/options";
import {
  mapColorModeToWasm,
  mapColorFilterToWasm,
} from "../utils/colorMappers";

interface UseMirrorWebcamParams {
  settings: SettingsConfig;
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  streamRef: MutableRefObject<MediaStream | null>;
  lastFrameTimeRef: MutableRefObject<number>;
  setIsWebcamRunning: (running: boolean) => void;
  setError: (error: string) => void;
  wasmInitialized: boolean;
  isWebcamRunning: boolean;
}

export function useMirrorWebcam({
  settings,
  videoRef,
  canvasRef,
  streamRef,
  lastFrameTimeRef,
  setIsWebcamRunning,
  setError,
  wasmInitialized,
  isWebcamRunning,
}: UseMirrorWebcamParams) {
  const devAutoStartRef = useRef(false);
  const [permissionGranted, setPermissionGranted] = useState(false);

  const startWebcam = useCallback(async () => {
    const clickTime = performance.now();
    console.log("[Mirror] Button clicked");
    console.time("[Mirror] Total startWebcam time");

    if (!videoRef.current || !canvasRef.current) {
      setError("Video or canvas element not ready");
      return;
    }

    try {
      console.time("[Mirror] WASM settings");
      if (isWasmReady() && isOptionsInitialized()) {
        // Apply WASM settings but don't fail if any individual setter fails
        // The webcam should start regardless of color settings
        try {
          setColorMode(mapColorModeToWasm(settings.colorMode));
        } catch (err) {
          console.warn("[Mirror] Failed to set color mode:", err);
        }
        try {
          setColorFilter(mapColorFilterToWasm(settings.colorFilter));
        } catch (err) {
          console.warn("[Mirror] Failed to set color filter:", err);
        }
        try {
          setPalette(settings.palette);
        } catch (err) {
          console.warn("[Mirror] Failed to set palette:", err);
        }
        if (settings.palette === "custom" && settings.paletteChars) {
          try {
            setPaletteChars(settings.paletteChars);
          } catch (err) {
            console.warn("[Mirror] Failed to set palette chars:", err);
          }
        }
        try {
          setMatrixRain(settings.matrixRain ?? false);
        } catch (err) {
          console.warn("[Mirror] Failed to set matrix rain:", err);
        }
        try {
          const isMacOS = /Mac|iPhone|iPad|iPod/.test(navigator.userAgent);
          setFlipX(settings.flipX ?? isMacOS);
        } catch (err) {
          console.warn("[Mirror] Failed to set flip X:", err);
        }
      } else if (!isOptionsInitialized()) {
        console.warn("[Mirror] WASM options not yet initialized, skipping settings");
      }
      console.timeEnd("[Mirror] WASM settings");

      // Check for test mode via query parameter
      const isTestMode = new URLSearchParams(window.location.search).has(
        "test",
      );

      if (isTestMode) {
        console.log("[Mirror] Test mode enabled - generating synthetic frames");
        const canvas = canvasRef.current!;
        canvas.width =
          settings.width && settings.width > 0 ? settings.width : 640;
        canvas.height =
          settings.height && settings.height > 0 ? settings.height : 480;
        console.log(
          `[Mirror] canvas set to ${canvas.width}x${canvas.height} (test mode)`,
        );
        lastFrameTimeRef.current = performance.now();
        setIsWebcamRunning(true);
        console.timeEnd("[Mirror] Total startWebcam time");
        return;
      }

      console.log("[Mirror] Calling getUserMedia...");
      console.time("[Mirror] getUserMedia (incl browser permission)");
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: settings.width },
          height: { ideal: settings.height },
          facingMode: "user",
        },
        audio: false,
      });
      console.timeEnd("[Mirror] getUserMedia (incl browser permission)");
      console.log(
        `[Mirror] Stream received after ${performance.now() - clickTime}ms from button click`,
      );

      streamRef.current = stream;
      videoRef.current.srcObject = stream;

      console.time("[Mirror] loadedmetadata");
      await new Promise<void>((resolve) => {
        videoRef.current!.addEventListener(
          "loadedmetadata",
          () => {
            console.timeEnd("[Mirror] loadedmetadata");
            const canvas = canvasRef.current!;
            // Use requested dimensions from settings, not video.videoWidth
            // (which are 2x2 when loadedmetadata fires, before actual stream loads)
            // Fallback to 640x480 if settings are invalid (0 or undefined)
            canvas.width =
              settings.width && settings.width > 0 ? settings.width : 640;
            canvas.height =
              settings.height && settings.height > 0 ? settings.height : 480;
            console.log(
              `[Mirror] canvas set to ${canvas.width}x${canvas.height} (from settings or defaults)`,
            );
            resolve();
          },
          { once: true },
        );
      });

      lastFrameTimeRef.current = performance.now();
      setIsWebcamRunning(true);
      console.timeEnd("[Mirror] Total startWebcam time");
    } catch (err) {
      setError(`Failed to start webcam: ${String(err)}`);
    }
  }, [
    settings,
    videoRef,
    canvasRef,
    streamRef,
    lastFrameTimeRef,
    setIsWebcamRunning,
    setError,
  ]);

  // Request camera permission early on page load (browsers require explicit permission)
  // This helps Firefox and other browsers allow camera access before auto-start
  useEffect(() => {
    const requestPermission = async () => {
      try {
        console.log("[Mirror] Requesting camera permission on page load");
        // Request minimal 1x1 stream just to trigger permission dialog
        const stream = await navigator.mediaDevices.getUserMedia({
          video: { width: { ideal: 1 }, height: { ideal: 1 } },
          audio: false,
        });
        console.log("[Mirror] Permission granted, stopping stream");
        stream.getTracks().forEach((track) => track.stop());
        // Add a small delay to ensure Firefox properly releases the camera
        // before the auto-start tries to access it
        await new Promise((resolve) => setTimeout(resolve, 100));
        setPermissionGranted(true);
      } catch (err) {
        const message = err instanceof Error ? err.message : String(err);
        console.log(
          "[Mirror] Permission request failed (user may have denied):",
          message,
        );
        // Even if permission request fails, proceed with auto-start
        // (user may deny and then manually grant, or have already granted)
        setPermissionGranted(true);
      }
    };
    void requestPermission();
  }, []);

  // Auto-start webcam in development mode
  useEffect(() => {
    if (
      import.meta.env["NODE_ENV"] !== "production" &&
      wasmInitialized &&
      permissionGranted &&
      !isWebcamRunning &&
      !devAutoStartRef.current
    ) {
      devAutoStartRef.current = true;
      console.log("[Mirror] Auto-starting webcam in development mode");
      void Promise.resolve().then(() => startWebcam());
    }
  }, [wasmInitialized, permissionGranted, isWebcamRunning, startWebcam]);

  return { startWebcam };
}
