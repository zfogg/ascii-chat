import { useCallback, useEffect, useRef, useState } from "react";
import { ConnectionState, PacketType } from "../wasm/client";
import {
  ClientConnection,
  H265Encoder,
  buildStreamStartPacket,
  buildImageFramePayload,
  buildImageFrameH265Payload,
} from "../network";
import type { SettingsConfig } from "../components";

// Helper to compute simple frame hash
const computeFrameHash = (data: Uint8Array): number => {
  let hash = 0;
  // Sample every 256th byte for speed
  for (let i = 0; i < data.length; i += 256) {
    hash = (hash << 5) - hash + (data[i] ?? 0);
    hash = hash & hash;
  }
  return Math.abs(hash);
};

interface UseWebcamStreamOptions {
  clientRef: React.RefObject<ClientConnection | null>;
  connectionState: ConnectionState;
  settings: SettingsConfig;
  captureFrame: () => {
    data: Uint8Array;
    width: number;
    height: number;
  } | null;
  canvasRef: React.RefObject<HTMLCanvasElement | null>;
  videoRef: React.RefObject<HTMLVideoElement | null>;
  frameIntervalRef: React.MutableRefObject<number>;
  lastFrameTimeRef: React.MutableRefObject<number>;
  frameQueueRef: React.MutableRefObject<string[]>;
  setError: (error: string) => void;
}

export function useWebcamStream(options: UseWebcamStreamOptions) {
  const {
    clientRef,
    connectionState,
    settings,
    captureFrame,
    canvasRef,
    videoRef,
    frameIntervalRef,
    lastFrameTimeRef,
    frameQueueRef,
    setError,
  } = options;

  const streamRef = useRef<MediaStream | null>(null);
  const h265EncoderRef = useRef<H265Encoder | null>(null);
  const webcamCaptureLoopRef = useRef<(() => void) | null>(null);
  const captureTimerRef = useRef<ReturnType<typeof setInterval> | null>(null);

  const captureLoopCountRef = useRef(0);
  const captureLoopFrameCountRef = useRef(0);
  const lastFrameHashRef = useRef(0);
  const uniqueFrameCountRef = useRef(0);
  const videFrameUpdateCountRef = useRef(0); // Track actual VIDEO updates

  const [isWebcamRunning, setIsWebcamRunning] = useState(false);

  // Inner loop function that doesn't have dependencies - this prevents RAF recursion from breaking
  const createWebcamCaptureLoop = useCallback(() => {
    let lastLogTime = performance.now();

    // Timer-based frame sending to match server render loop (not RAF-based)
    // RAF fires at monitor refresh rate (60+ Hz) regardless of frame send interval
    // Timer ensures we send at exactly the target FPS, matching C client behavior
    const sendOneFrame = () => {
      const now = performance.now();

      // Log every 100ms regardless of frame sends
      if (now - lastLogTime > 100) {
        lastLogTime = now;
        console.log(
          `[Client] Frame send: count=${captureLoopFrameCountRef.current}, unique=${uniqueFrameCountRef.current}, video_updates=${videFrameUpdateCountRef.current}, ready=${
            !!clientRef.current && connectionState === ConnectionState.CONNECTED
          }`,
        );
      }

      // Call captureAndSendFrame through ref to get the latest version
      const conn = clientRef.current;
      if (conn && connectionState === ConnectionState.CONNECTED) {
        const frame = captureFrame();
        if (frame && frame.data) {
          captureLoopFrameCountRef.current++;
          const frameHash = computeFrameHash(frame.data);

          // Log EVERY frame sent, not just unique ones
          const isNewFrame = frameHash !== lastFrameHashRef.current;
          if (isNewFrame) {
            uniqueFrameCountRef.current++;
            videFrameUpdateCountRef.current++;
            lastFrameHashRef.current = frameHash;
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (UNIQUE #${uniqueFrameCountRef.current}): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          } else {
            // console.log(
            //   `[Client] SEND #${captureLoopFrameCountRef.current} (DUPLICATE): hash=0x${frameHash.toString(
            //     16,
            //   )}, size=${frame.data.length}`,
            // );
          }

          // Try H.265 encoding if available (but prioritize RGBA for stability)
          // H.265 encoding can be slow on some systems, so we always have RGBA fallback
          // Set window.DISABLE_H265 = true in console to disable H.265 encoding
          const h265Disabled =
            typeof window !== "undefined" &&
            (window as unknown as Record<string, unknown>)["DISABLE_H265"] ===
              true;
          let sentH265 = false;
          if (
            !h265Disabled &&
            h265EncoderRef.current &&
            H265Encoder.isSupported()
          ) {
            try {
              // Drain chunks encoded from PREVIOUS frame iteration(s).
              // The encoder is async - when we call encode(), the data arrives
              // via the output callback in the background. So we must drain
              // BEFORE calling encode() on the new frame.
              const chunks = h265EncoderRef.current.drain();
              if (chunks.length > 0) {
                for (const chunk of chunks) {
                  const payload = buildImageFrameH265Payload(
                    chunk.flags,
                    chunk.width,
                    chunk.height,
                    chunk.data,
                  );
                  conn.sendPacket(PacketType.IMAGE_FRAME_H265, payload);
                }
                sentH265 = true;
              }

              // Queue current frame for encoding. This returns immediately,
              // and the encoded data will be available in drain() on the
              // next iteration.
              if (!canvasRef.current) {
                throw new Error("Canvas not available for VideoFrame creation");
              }

              // Create VideoFrame from canvas for H.265 encoding
              const videoFrame = new VideoFrame(canvasRef.current, {
                timestamp: now * 1000, // microseconds
              });

              // Request keyframe every 60 frames
              const forceKeyframe = captureLoopFrameCountRef.current % 60 === 0;
              h265EncoderRef.current.encode(videoFrame, forceKeyframe);
              videoFrame.close();
            } catch (err) {
              console.error(
                "[Client] H.265 encoding failed, will use RGBA:",
                err,
              );
              // Disable H.265 for rest of session if encoding fails
              if (h265EncoderRef.current) {
                h265EncoderRef.current.destroy();
                h265EncoderRef.current = null;
              }
            }
          }

          // Always send RGBA if H.265 didn't produce chunks or failed
          if (!sentH265) {
            const payload = buildImageFramePayload(
              frame.data,
              frame.width,
              frame.height,
            );

            try {
              conn.sendPacket(PacketType.IMAGE_FRAME, payload);
            } catch (err) {
              console.error("[Client] Failed to send IMAGE_FRAME:", err);
            }
          }
        } else {
          console.warn(
            `[Client] captureFrame returned null at call ${captureLoopCountRef.current}`,
          );
        }
      }
    };

    return sendOneFrame;
  }, [captureFrame, connectionState, clientRef, canvasRef]);

  // Create capture function ref
  useEffect(() => {
    webcamCaptureLoopRef.current = createWebcamCaptureLoop();
  }, [createWebcamCaptureLoop]);

  // Start/stop timer when connection changes
  useEffect(() => {
    if (
      connectionState === ConnectionState.CONNECTED &&
      webcamCaptureLoopRef.current
    ) {
      // Start timer to send frames at target FPS
      const sendInterval = 1000 / settings.targetFps;
      captureTimerRef.current = setInterval(() => {
        if (webcamCaptureLoopRef.current) {
          webcamCaptureLoopRef.current();
        }
      }, sendInterval);
      console.log(
        `[Client] Started frame send timer: ${sendInterval.toFixed(1)}ms interval (${settings.targetFps} FPS)`,
      );
    } else {
      // Stop timer when disconnected
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
        console.log("[Client] Stopped frame send timer");
      }
    }

    return () => {
      if (captureTimerRef.current) {
        clearInterval(captureTimerRef.current);
        captureTimerRef.current = null;
      }
    };
  }, [connectionState, settings.targetFps]);

  const startWebcam = useCallback(async () => {
    console.log("[Client] startWebcam() called");
    console.log(
      `[DEBUG] videoRef.current=${!!videoRef.current}, canvasRef.current=${!!canvasRef.current}`,
    );

    if (!videoRef.current || !canvasRef.current) {
      console.error("[Client] Video or canvas element not ready");
      setError("Video or canvas element not ready");
      return;
    }

    console.log(
      `[DEBUG] connectionState=${connectionState} vs CONNECTED=${ConnectionState.CONNECTED}`,
    );
    if (connectionState !== ConnectionState.CONNECTED) {
      console.error(
        `[Client] Not connected (state=${connectionState}), cannot start webcam`,
      );
      setError("Must be connected to server before starting webcam");
      return;
    }

    console.log("[Client] Passed all initial checks");

    try {
      // Send STREAM_START to notify server we're about to send video
      if (clientRef.current) {
        console.log("[Client] Sending STREAM_START before webcam...");
        const streamPayload = buildStreamStartPacket(false);
        // Send as unencrypted ACIP packet (like native client does)
        clientRef.current.sendUnencryptedAcipPacket(
          PacketType.STREAM_START,
          streamPayload,
        );
        console.log("[Client] STREAM_START sent");
      } else {
        console.log(
          "[Client] clientRef.current is null, skipping STREAM_START",
        );
      }

      const w = settings.width || 1280;
      const h = settings.height || 720;
      console.log(`[Client] Requesting webcam stream: ${w}x${h}`);
      console.log(
        `[DEBUG] videoRef.current before getUserMedia:`,
        videoRef.current,
      );

      let stream;
      try {
        stream = await navigator.mediaDevices.getUserMedia({
          video: { width: { ideal: w }, height: { ideal: h } },
          audio: false,
        });
      } catch (err) {
        console.error(
          "[Client] getUserMedia failed (trying fallback without constraints):",
          err,
        );
        try {
          stream = await navigator.mediaDevices.getUserMedia({
            video: true,
            audio: false,
          });
        } catch (err2) {
          console.error("[Client] getUserMedia failed completely:", err2);
          throw err2;
        }
      }

      console.log("[Client] Webcam stream acquired");
      console.log(`[DEBUG] Stream object:`, stream);
      console.log(
        `[Client] Stream tracks: ${stream.getTracks().length}, active=${stream.active}`,
      );

      // Log track details
      stream.getTracks().forEach((track, idx) => {
        console.log(`[DEBUG] Track ${idx}:`, {
          kind: track.kind,
          enabled: track.enabled,
          readyState: track.readyState,
          label: track.label,
          settings: track.getSettings ? track.getSettings() : "N/A",
        });
      });

      streamRef.current = stream;
      const video = videoRef.current!;
      console.log("[DEBUG] Before setting srcObject, video element:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        networkState: video.networkState,
      });

      video.srcObject = stream;
      console.log("[DEBUG] After setting srcObject");
      console.log("[DEBUG] Video element after srcObject:", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        srcObject: !!video.srcObject,
      });

      // Monitor stream for unexpected end
      stream.getTracks().forEach((track) => {
        track.onended = () => {
          console.warn(
            `[Client] Media track ended (${track.kind}): readyState=${track.readyState}`,
          );
        };
        track.onmute = () => {
          console.warn(`[Client] Media track muted (${track.kind})`);
        };
        track.onunmute = () => {
          console.log(`[Client] Media track unmuted (${track.kind})`);
        };
      });

      // Set up metadata listener BEFORE playing to catch the event
      const metadataPromise = new Promise<void>((resolve) => {
        const handleMetadata = () => {
          const video = videoRef.current!;
          const canvas = canvasRef.current!;
          console.log(
            `[Client] Webcam metadata loaded: ${video.videoWidth}x${video.videoHeight}, videoTime=${video.currentTime}, paused=${video.paused}`,
          );
          console.log("[DEBUG] Metadata event - full video element state:", {
            videoWidth: video.videoWidth,
            videoHeight: video.videoHeight,
            readyState: video.readyState,
            networkState: video.networkState,
            currentTime: video.currentTime,
            duration: video.duration,
            paused: video.paused,
            srcObject: !!video.srcObject,
            src: video.src,
          });

          // Check if dimensions are valid before resizing
          if (video.videoWidth > 0 && video.videoHeight > 0) {
            canvas.width = video.videoWidth;
            canvas.height = video.videoHeight;
            console.log(
              `[Client] Canvas resized to: ${canvas.width}x${canvas.height}`,
            );
            console.log("[DEBUG] Canvas state after resize:", {
              width: canvas.width,
              height: canvas.height,
              clientWidth: canvas.clientWidth,
              clientHeight: canvas.clientHeight,
            });
          } else {
            console.warn(
              `[DEBUG] Invalid video dimensions: ${video.videoWidth}x${video.videoHeight}, not resizing canvas`,
            );
          }
          video.removeEventListener("loadedmetadata", handleMetadata);
          resolve();
        };
        videoRef.current!.addEventListener("loadedmetadata", handleMetadata);
        console.log("[DEBUG] loadedmetadata listener attached");
      });

      // Now play the video (metadata event may already be queued)
      console.log("[Client] Attempting to play video...");
      console.log("[DEBUG] Video element before play():", {
        videoWidth: video.videoWidth,
        videoHeight: video.videoHeight,
        readyState: video.readyState,
        paused: video.paused,
        srcObject: !!video.srcObject,
      });
      try {
        await video.play();
        console.log("[Client] Video is playing");
        console.log("[DEBUG] Video element after play():", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          paused: video.paused,
        });
      } catch (playErr) {
        console.error("[Client] Video play failed (may be expected):", playErr);
        console.error("[DEBUG] Video state when play() failed:", {
          videoWidth: video.videoWidth,
          videoHeight: video.videoHeight,
          readyState: video.readyState,
          srcObject: !!video.srcObject,
        });
      }

      // Wait for metadata with a timeout (5 seconds) in case it never fires
      const timeoutPromise = new Promise<void>((resolve) => {
        setTimeout(() => {
          console.warn(
            "[Client] Metadata timeout - setting canvas dimensions from current video properties",
          );
          if (videoRef.current && canvasRef.current) {
            const video = videoRef.current;
            const canvas = canvasRef.current;
            if (video.videoWidth > 0 && video.videoHeight > 0) {
              canvas.width = video.videoWidth;
              canvas.height = video.videoHeight;
              console.log(
                `[Client] Canvas resized from timeout: ${canvas.width}x${canvas.height}`,
              );
            }
          }
          resolve();
        }, 5000);
      });

      await Promise.race([metadataPromise, timeoutPromise]);

      // Validate that canvas has valid dimensions before proceeding
      if (
        !canvasRef.current ||
        canvasRef.current.width === 0 ||
        canvasRef.current.height === 0
      ) {
        const video = videoRef.current;
        console.warn(
          `[startWebcam] Canvas dimensions still invalid after metadata wait: canvas=${canvasRef.current?.width}x${canvasRef.current?.height}, video=${video?.videoWidth}x${video?.videoHeight}`,
        );
        // If video has dimensions, use them as fallback
        if (video && video.videoWidth > 0 && video.videoHeight > 0) {
          if (canvasRef.current) {
            canvasRef.current.width = video.videoWidth;
            canvasRef.current.height = video.videoHeight;
            console.log(
              `[startWebcam] Using video dimensions as fallback: ${video.videoWidth}x${video.videoHeight}`,
            );
          }
        } else {
          // Video still has no dimensions - cannot proceed
          throw new Error(
            "Failed to obtain video dimensions after 5-second wait. Browser may not have granted camera permissions or device is not available.",
          );
        }
      }

      console.log(
        `[startWebcam] About to start capture loop, videoRef=${
          videoRef.current ? "OK" : "NULL"
        }, canvasRef=${canvasRef.current ? "OK" : "NULL"}`,
      );
      if (videoRef.current) {
        console.log(
          `[startWebcam] Video: playing=${!videoRef.current
            .paused}, width=${videoRef.current.videoWidth}, height=${videoRef.current.videoHeight}`,
        );
      }

      setIsWebcamRunning(true);
      lastFrameTimeRef.current = performance.now();
      frameIntervalRef.current = 1000 / settings.targetFps;
      frameQueueRef.current = [];

      // Initialize H.265 encoder if supported
      if (H265Encoder.isSupported() && canvasRef.current && videoRef.current) {
        try {
          const w = canvasRef.current.width || 1280;
          const h = canvasRef.current.height || 720;
          console.log(
            `[Client] Initializing H.265 encoder: ${w}x${h} @ ${settings.targetFps} FPS`,
          );
          h265EncoderRef.current = new H265Encoder();
          await h265EncoderRef.current.initialize(w, h, settings.targetFps);
          console.log("[Client] H.265 encoder initialized successfully");
        } catch (err) {
          console.error("[Client] H.265 encoder initialization failed:", err);
          h265EncoderRef.current?.destroy();
          h265EncoderRef.current = null;
        }
      } else if (!H265Encoder.isSupported()) {
        console.log(
          "[Client] H.265 encoding not supported in this browser, using RGBA fallback",
        );
      }

      console.log("[Client] Starting render loops...");
      console.log(
        `[Client] frameInterval set to: ${frameIntervalRef.current}ms (${settings.targetFps} FPS)`,
      );

      // Log stream state before starting capture
      if (streamRef.current) {
        console.log(
          `[Client] Stream state before capture: active=${streamRef.current.active}, tracks=${streamRef.current.getTracks().length}`,
        );
        streamRef.current.getTracks().forEach((track) => {
          console.log(
            `[Client] Track (${track.kind}): readyState=${track.readyState}, enabled=${track.enabled}, muted=${track.muted}`,
          );
        });
      }

      console.log("[Client] Webcam started successfully");
    } catch (err) {
      const errMsg = `Failed to start webcam: ${String(err)}`;
      console.error("[Client]", errMsg);
      console.error("[Client] Error:", err);
      setError(errMsg);
    }
  }, [
    connectionState,
    settings.width,
    settings.height,
    settings.targetFps,
    videoRef,
    canvasRef,
    clientRef,
    frameIntervalRef,
    lastFrameTimeRef,
    frameQueueRef,
    setError,
  ]);

  const stopWebcam = useCallback(() => {
    // Stop timer (connection state change will also stop it)
    if (captureTimerRef.current) {
      clearInterval(captureTimerRef.current);
      captureTimerRef.current = null;
    }

    if (streamRef.current) {
      streamRef.current.getTracks().forEach((track) => track.stop());
      streamRef.current = null;
    }

    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }

    // Clean up H.265 encoder
    if (h265EncoderRef.current) {
      h265EncoderRef.current.destroy();
      h265EncoderRef.current = null;
    }

    frameQueueRef.current = [];
    setIsWebcamRunning(false);
  }, [videoRef, frameQueueRef]);

  return {
    startWebcam,
    stopWebcam,
    isWebcamRunning,
  };
}
