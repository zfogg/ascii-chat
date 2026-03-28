import { useCallback, useRef, MutableRefObject } from "react";

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
  const loopDebugRef = useRef({ count: 0, lastLog: 0, skipped: 0 });

  const animationFrameRef = useCallback(
    (time: number) => {
      try {
        const elapsed = time - lastFrameTimeRef.current;
        const interval = frameIntervalRef.current;
        const debug = loopDebugRef.current;

        debug.count++;

        if (elapsed >= interval) {
          lastFrameTimeRef.current = time;
          renderFrame(elapsed);
        } else {
          debug.skipped++;
        }

        // Schedule next frame
        requestAnimationFrame(animationFrameRef);
      } catch (error) {
        if (onError) {
          onError(error);
        } else {
          console.error("Render loop error:", error);
        }
      }
    },
    [renderFrame, onError, frameIntervalRef, lastFrameTimeRef, loopDebugRef],
  );

  const startRenderLoop = useCallback(() => {
    lastFrameTimeRef.current = performance.now();
    requestAnimationFrame(animationFrameRef);
  }, [lastFrameTimeRef, animationFrameRef]);

  return { startRenderLoop };
}
