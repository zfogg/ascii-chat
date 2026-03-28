import React, { useCallback, useRef } from "react";

/**
 * Hook for capturing video frames from a canvas
 * Handles common canvas setup and frame capture logic
 */
export function useCanvasCapture(
  videoRef: React.RefObject<HTMLVideoElement | null>,
  canvasRef: React.RefObject<HTMLCanvasElement | null>,
) {
  const capturedDataRef = useRef<{
    data: Uint8Array;
    width: number;
    height: number;
  } | null>(null);

  const captureFrame = useCallback((): {
    data: Uint8Array;
    width: number;
    height: number;
  } | null => {
    const capStartTime = performance.now();
    const video = videoRef.current;
    const canvas = canvasRef.current;

    if (!video || !canvas) {
      console.warn(
        `[useCanvasCapture] captureFrame at ${capStartTime.toFixed(0)}ms: Video or canvas not ready (video=${!!video}, canvas=${!!canvas})`,
      );
      return null;
    }

    if (canvas.width === 0 || canvas.height === 0) {
      console.warn(
        "[useCanvasCapture] Canvas dimensions not set:",
        canvas.width,
        canvas.height,
      );
      return null;
    }

    const ctx = canvas.getContext("2d", { willReadFrequently: true });
    if (!ctx) {
      console.error("[useCanvasCapture] Failed to get canvas 2D context");
      return null;
    }

    try {
      // Verify video has data
      if (video.videoWidth === 0 || video.videoHeight === 0) {
        console.warn(
          "[useCanvasCapture] Video not ready - no dimensions:",
          video.videoWidth,
          video.videoHeight,
        );
        return null;
      }

      // Verify canvas has valid dimensions
      if (canvas.width === 0 || canvas.height === 0) {
        console.error(
          "[useCanvasCapture] Canvas has invalid dimensions after setup:",
          `${canvas.width}x${canvas.height}. This should have been initialized with video capture dimensions.`,
        );
        return null;
      }

      ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
      const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);

      // Verify imageData has expected size
      const expectedBytes = canvas.width * canvas.height * 4;
      if (imageData.data.length !== expectedBytes) {
        console.error(
          `[useCanvasCapture] ImageData size mismatch: expected ${expectedBytes} bytes for ${canvas.width}x${canvas.height}, got ${imageData.data.length}`,
        );
        return null;
      }

      const rgbaData = new Uint8Array(imageData.data);

      // Verify RGBA data before returning
      const expectedSize = canvas.width * canvas.height * 4;
      const actualSize = rgbaData.length;
      if (actualSize !== expectedSize) {
        console.error(
          `[useCanvasCapture] CRITICAL: RGBA data size mismatch - expected ${expectedSize} (${canvas.width}x${canvas.height}*4), got ${actualSize}`,
        );
        return null;
      }

      return {
        data: rgbaData,
        width: canvas.width,
        height: canvas.height,
      };
    } catch (err) {
      console.error("[useCanvasCapture] Failed to capture frame:", err);
      if (err instanceof Error && err.message.includes("cross-origin")) {
        console.error(
          "[useCanvasCapture] Cross-origin error - check CORS settings",
        );
      }
      return null;
    }
  }, [videoRef, canvasRef]);

  return { captureFrame, capturedDataRef };
}
