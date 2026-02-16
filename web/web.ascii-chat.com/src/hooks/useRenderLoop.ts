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
  const animationFrameRef = useCallback(
    (time: number) => {
      try {
        const elapsed = time - lastFrameTimeRef.current;

        if (elapsed >= frameIntervalRef.current) {
          lastFrameTimeRef.current = time;
          renderFrame();
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
