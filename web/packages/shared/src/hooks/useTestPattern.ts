import { useCallback, useMemo } from "react";
import {
  drawSelectedTestPatternFrame,
  getTestPatternMode,
  type TestPatternMode,
} from "../testPattern";

/** Selects the shared synthetic video animation for mirror, client, and discovery. */
export function useTestPattern(search = window.location.search): {
  mode: TestPatternMode;
  enabled: boolean;
  drawFrame: (
    context: CanvasRenderingContext2D,
    width: number,
    height: number,
    time?: number,
  ) => void;
} {
  const mode = useMemo(() => getTestPatternMode(search), [search]);
  const drawFrame = useCallback(
    (
      context: CanvasRenderingContext2D,
      width: number,
      height: number,
      time?: number,
    ) => {
      if (mode !== "none")
        drawSelectedTestPatternFrame(mode, context, width, height, time);
    },
    [mode],
  );
  return useMemo(
    () => ({ mode, enabled: mode !== "none", drawFrame }),
    [mode, drawFrame],
  );
}
