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
  const loopDebugRef = useRef({
    count: 0,
    lastLog: 0,
    skipped: 0,
    rendered: 0,
  });
  const frameRef = useRef<number | null>(null);
  useEffect(
    () => () => {
      if (frameRef.current !== null) cancelAnimationFrame(frameRef.current);
      frameRef.current = null;
    },
    [],
  );

  const animationFrameRef = useCallback(
    (time: number) => {
      try {
        const elapsed = time - lastFrameTimeRef.current;
        const interval = frameIntervalRef.current;
        const debug = loopDebugRef.current;

        debug.count++;

        // Allow for RAF timestamp rounding and retain the scheduled cadence.
        // Resetting to the current time loses fractional intervals and skips
        // refreshes when the display rate is close to the target rate.
        if (elapsed + 1 >= interval) {
          const intervals = Math.max(1, Math.floor((elapsed + 1) / interval));
          lastFrameTimeRef.current += intervals * interval;
          debug.rendered++;
          renderFrame(elapsed);
        } else {
          debug.skipped++;
        }

        if (!debug.lastLog) debug.lastLog = time;
        if (time - debug.lastLog >= 1000) {
          console.info(
            "[RenderLoopTiming]",
            JSON.stringify({
              rafFps: Number(
                ((debug.count * 1000) / (time - debug.lastLog)).toFixed(1),
              ),
              renderAttempts: debug.rendered,
              skipped: debug.skipped,
              elapsedMs: Math.round(time - debug.lastLog),
            }),
          );
          debug.count = 0;
          debug.rendered = 0;
          debug.skipped = 0;
          debug.lastLog = time;
        }

        // Schedule next frame
        frameRef.current = requestAnimationFrame(animationFrameRef);
      } catch (error) {
        frameRef.current = null;
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
    if (frameRef.current !== null) cancelAnimationFrame(frameRef.current);
    lastFrameTimeRef.current = performance.now();
    frameRef.current = requestAnimationFrame(animationFrameRef);
  }, [lastFrameTimeRef, animationFrameRef]);

  return { startRenderLoop };
}
