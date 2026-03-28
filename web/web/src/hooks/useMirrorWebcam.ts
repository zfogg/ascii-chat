import {
  type MutableRefObject,
  type RefObject,
  useCallback,
  useEffect,
  useRef,
  useState,
} from "react";
import type { SettingsConfig } from "../components";
import { type MediaSource, MediaSourceType } from "./useClientLike";
import {
  isOptionsInitialized,
  isWasmReady,
  setColorFilter,
  setColorMode,
  setFlipX,
  setMatrixRain,
  setPalette,
  setPaletteChars,
} from "@ascii-chat/shared/wasm";
import { mapColorFilterToWasm, mapColorModeToWasm } from "../utils";

interface UseMirrorWebcamParams {
  settings: SettingsConfig;
  videoRef: RefObject<HTMLVideoElement | null>;
  canvasRef: RefObject<HTMLCanvasElement | null>;
  streamRef: MutableRefObject<MediaStream | null>;
  objectUrlRef: MutableRefObject<string | null>;
  lastFrameTimeRef: MutableRefObject<number>;
  setIsWebcamRunning: (running: boolean) => void;
  setMediaSource: (source: MediaSource) => void;
  setError: (error: string) => void;
  wasmInitialized: boolean;
  isWebcamRunning: boolean;
  terminalDimensions?: { cols: number; rows: number };
}

export function useMirrorWebcam({
  settings,
  videoRef,
  canvasRef,
  streamRef,
  objectUrlRef,
  lastFrameTimeRef,
  setIsWebcamRunning,
  setMediaSource,
  setError,
  wasmInitialized,
  isWebcamRunning,
  terminalDimensions,
}: UseMirrorWebcamParams) {
  const devAutoStartRef = useRef(false);
  const [permissionGranted, setPermissionGranted] = useState(false);

  const startWebcam = useCallback(async () => {
    const clickTime = performance.now();
    console.log(`[Mirror] Button clicked / startWebcam called at ${clickTime}`);
    console.time("[Mirror] Total startWebcam time");

    console.log(
      `[Mirror] Checking videoRef=${!!videoRef.current}, canvasRef=${!!canvasRef.current}`,
    );
    if (!videoRef.current || !canvasRef.current) {
      console.error("[Mirror] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return;
    }

    try {
      console.log(
        `[Mirror] startWebcam: entering try block at ${performance.now()}`,
      );
      console.time("[Mirror] WASM settings");
      console.log(
        `[Mirror] WASM ready=${isWasmReady()}, options initialized=${isOptionsInitialized()}`,
      );
      if (isWasmReady() && isOptionsInitialized()) {
        // Apply WASM settings but don't fail if any individual setter fails
        // The webcam should start regardless of color settings
        try {
          console.log("[Mirror] Setting color mode...");
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
          setFlipX(settings.flipX ?? true);
        } catch (err) {
          console.warn("[Mirror] Failed to set flip X:", err);
        }
      } else if (!isOptionsInitialized()) {
        console.warn(
          "[Mirror] WASM options not yet initialized, skipping settings",
        );
      }
      console.timeEnd("[Mirror] WASM settings");

      // Check for test mode via query parameter
      console.log(`[Mirror] Checking test mode at ${performance.now()}`);
      const isTestMode = new URLSearchParams(window.location.search).has(
        "test",
      );
      console.log(`[Mirror] isTestMode=${isTestMode}`);

      if (isTestMode) {
        console.log(
          `[Mirror] Test mode enabled at ${performance.now()} - generating synthetic frames`,
        );
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
        setMediaSource(MediaSourceType.WEBCAM);
        console.timeEnd("[Mirror] Total startWebcam time");
        return;
      }

      console.log(
        `[Mirror] About to call getUserMedia at ${performance.now()}`,
      );
      console.time("[Mirror] getUserMedia (incl browser permission)");
      console.log("[Mirror] Calling getUserMedia...");
      const stream = await navigator.mediaDevices.getUserMedia({
        video: {
          width: { ideal: settings.width },
          height: { ideal: settings.height },
          facingMode: "user",
        },
        audio: false,
      });
      console.timeEnd("[Mirror] getUserMedia (incl browser permission)");
      console.log(`[Mirror] getUserMedia returned at ${performance.now()}`);
      console.log(
        `[Mirror] Stream received after ${
          performance.now() - clickTime
        }ms from button click`,
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
      setMediaSource(MediaSourceType.WEBCAM);
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
    setMediaSource,
    setError,
  ]);

  // DISABLED: Permission request on page load was blocking React for 30+ seconds
  // The getUserMedia call would hang waiting for browser permission dialog.
  // Instead, we'll request permission when user clicks "Start Webcam" button.
  // useEffect(() => {
  //   const requestPermission = async () => {
  //     try {
  //       console.log("[Mirror] Requesting camera permission on page load");
  //       // Request minimal 1x1 stream just to trigger permission dialog
  //       const stream = await navigator.mediaDevices.getUserMedia({
  //         video: { width: { ideal: 1 }, height: { ideal: 1 } },
  //         audio: false,
  //       });
  //       console.log("[Mirror] Permission granted, stopping stream");
  //       stream.getTracks().forEach((track) => track.stop());
  //       // Add a small delay to ensure Firefox properly releases the camera
  //       // before the auto-start tries to access it
  //       await new Promise((resolve) => setTimeout(resolve, 100));
  //       setPermissionGranted(true);
  //     } catch (err) {
  //       const message = err instanceof Error ? err.message : String(err);
  //       console.log(
  //         "[Mirror] Permission request failed (user may have denied):",
  //         message,
  //       );
  //       // Even if permission request fails, proceed with auto-start
  //       // (user may deny and then manually grant, or have already granted)
  //       setPermissionGranted(true);
  //     }
  //   };
  //   void requestPermission();
  // }, []);

  // Set permissionGranted immediately so auto-start can proceed
  // The startWebcam function will request permission when needed
  useEffect(() => {
    setPermissionGranted(true);
  }, []);

  // Auto-start webcam in development mode
  // CRITICAL: Wait for terminalDimensions to be set by AsciiRenderer before starting webcam.
  // If we start before dimensions are available, the render loop will block on 0x0 dimensions.
  useEffect(() => {
    const hasDimensions =
      terminalDimensions &&
      terminalDimensions.cols > 0 &&
      terminalDimensions.rows > 0;

    console.log(
      `[Mirror] useMirrorWebcam auto-start effect: NODE_ENV=${
        import.meta.env["NODE_ENV"]
      }, wasmInitialized=${wasmInitialized}, permissionGranted=${permissionGranted}, isWebcamRunning=${isWebcamRunning}, devAutoStartRef=${devAutoStartRef.current}, hasDimensions=${hasDimensions}`,
    );

    if (
      import.meta.env["NODE_ENV"] !== "production" &&
      wasmInitialized &&
      permissionGranted &&
      !isWebcamRunning &&
      !devAutoStartRef.current &&
      hasDimensions
    ) {
      devAutoStartRef.current = true;
      console.log(
        `[Mirror] Auto-starting webcam with dimensions ${terminalDimensions.cols}x${terminalDimensions.rows}`,
      );
      console.time("[Mirror] startWebcam()");
      void Promise.resolve().then(() => {
        console.log(`[Mirror] Calling startWebcam at ${performance.now()}`);
        startWebcam();
        console.timeEnd("[Mirror] startWebcam()");
      });
    }
  }, [
    wasmInitialized,
    permissionGranted,
    isWebcamRunning,
    startWebcam,
    terminalDimensions,
  ]);

  const startVideoFile = useCallback(
    async (file: File) => {
      console.log("[Mirror] Starting video file:", file.name);

      if (!videoRef.current || !canvasRef.current) {
        setError("Video or canvas element not ready");
        return;
      }

      try {
        if (isWasmReady() && isOptionsInitialized()) {
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
            // Video files should not be flipped (unlike webcam which mirrors)
            setFlipX(false);
          } catch (err) {
            console.warn("[Mirror] Failed to set flip X:", err);
          }
        }

        // Revoke any previous object URL
        if (objectUrlRef.current) {
          URL.revokeObjectURL(objectUrlRef.current);
        }

        const url = URL.createObjectURL(file);
        objectUrlRef.current = url;

        const video = videoRef.current;
        video.src = url;
        video.loop = true;
        video.muted = false;
        video.playsInline = true;

        await new Promise<void>((resolve, reject) => {
          const onLoaded = () => {
            const canvas = canvasRef.current!;
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            console.log(
              `[Mirror] Video file loaded: ${video.videoWidth}x${video.videoHeight}`,
            );
            resolve();
          };
          const onError = () => {
            reject(
              new Error(
                `Failed to load video: ${
                  video.error?.message || "Unknown error"
                }`,
              ),
            );
          };
          video.addEventListener("loadedmetadata", onLoaded, { once: true });
          video.addEventListener("error", onError, { once: true });
        });

        await video.play();

        lastFrameTimeRef.current = performance.now();
        setIsWebcamRunning(true);
        setMediaSource(MediaSourceType.FILE);
        console.log("[Mirror] Video file playing");
      } catch (err) {
        setError(`Failed to play video file: ${String(err)}`);
      }
    },
    [
      settings,
      videoRef,
      canvasRef,
      objectUrlRef,
      lastFrameTimeRef,
      setIsWebcamRunning,
      setMediaSource,
      setError,
    ],
  );

  return { startWebcam, startVideoFile };
}
