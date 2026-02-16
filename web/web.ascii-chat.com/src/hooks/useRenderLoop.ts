import { useCallback, MutableRefObject } from "react";

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
  const loopDebugRef = { count: 0, lastLog: 0, skipped: 0 };

  const animationFrameRef = useCallback(
    (time: number) => {
      try {
        const elapsed = time - lastFrameTimeRef.current;
        const interval = frameIntervalRef.current;

        loopDebugRef.count++;

        if (elapsed >= interval) {
          lastFrameTimeRef.current = time;
          renderFrame();
        } else {
          loopDebugRef.skipped++;
        }

        // Log every 60 cycles
        if (loopDebugRef.count - loopDebugRef.lastLog >= 60) {
          const skipRate = (
            (loopDebugRef.skipped / loopDebugRef.count) *
            100
          ).toFixed(1);
          console.log(
            `[RenderLoop] ${loopDebugRef.count} RAF cycles, ${skipRate}% skipped (interval=${interval.toFixed(2)}ms)`,
          );
          loopDebugRef.lastLog = loopDebugRef.count;
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
    [renderFrame, onError],
  );

  const startRenderLoop = useCallback(() => {
    lastFrameTimeRef.current = performance.now();
    requestAnimationFrame(animationFrameRef);
  }, [animationFrameRef]);

  return { startRenderLoop };
}
