import { useCallback, useEffect, useRef, MutableRefObject } from "react";

interface UseRenderLoopReturn {
  startRenderLoop: () => void;
}

/**
 * Custom hook for managing the render loop animation frame
 * Handles frame rate limiting and error handling
 */
export function useRenderLoop(
  renderFrame: (deltaMs: number) => void,
  frameIntervalRef: MutableRefObject<number>,
  lastFrameTimeRef: MutableRefObject<number>,
  onError?: (error: unknown) => void,
): UseRenderLoopReturn {
  const frameRef = useRef<number | null>(null);
  useEffect(
    () => () => {
      if (frameRef.current !== null) cancelAnimationFrame(frameRef.current);
      frameRef.current = null;
    },
    [],
  );

  const animationFrameRef = useCallback(
    function animate(time: number) {
      try {
        const elapsed = time - lastFrameTimeRef.current;
        const interval = frameIntervalRef.current;

        // Allow for RAF timestamp rounding and retain the scheduled cadence.
        // Resetting to the current time loses fractional intervals and skips
        // refreshes when the display rate is close to the target rate.
        if (elapsed + 1 >= interval) {
          const intervals = Math.max(1, Math.floor((elapsed + 1) / interval));
          lastFrameTimeRef.current += intervals * interval;
          renderFrame(elapsed);
        }

        // Schedule next frame
        frameRef.current = requestAnimationFrame(animate);
      } catch (error) {
        frameRef.current = null;
        if (onError) {
          onError(error);
        } else {
          console.error("Render loop error:", error);
        }
      }
    },
    [renderFrame, onError, frameIntervalRef, lastFrameTimeRef],
  );

  const startRenderLoop = useCallback(() => {
    if (frameRef.current !== null) cancelAnimationFrame(frameRef.current);
    lastFrameTimeRef.current = performance.now();
    frameRef.current = requestAnimationFrame(animationFrameRef);
  }, [lastFrameTimeRef, animationFrameRef]);

  return { startRenderLoop };
}
