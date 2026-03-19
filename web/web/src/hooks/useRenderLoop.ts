import { useCallback, useRef, MutableRefObject } from "react";

interface UseRenderLoopReturn {
  startRenderLoop: () => void;
}

/**
 * Custom hook for managing the render loop animation frame
 * Handles frame rate limiting and error handling
 */
export function useRenderLoop(
  renderFrame: () => void,
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
          renderFrame();
        } else {
          debug.skipped++;
        }

        // Log every 60 cycles
        if (debug.count - debug.lastLog >= 60) {
          const skipRate = ((debug.skipped / debug.count) * 100).toFixed(1);
          console.log(
            `[RenderLoop] ${debug.count} RAF cycles, ${skipRate}% skipped (interval=${interval.toFixed(2)}ms)`,
          );
          debug.lastLog = debug.count;
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
