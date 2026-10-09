import { useCallback, useMemo, useEffect, useRef } from "react";
import {
  drawSelectedTestPatternFrame,
  releaseTestPatternFrame,
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
  ) => ImageData | undefined;
} {
  const contexts = useRef(new Set<CanvasRenderingContext2D>());
  useEffect(
    () => () => {
      for (const context of contexts.current) releaseTestPatternFrame(context);
      contexts.current.clear();
    },
    [],
  );
  const mode = useMemo(() => getTestPatternMode(search), [search]);
  const drawFrame = useCallback(
    (
      context: CanvasRenderingContext2D,
      width: number,
      height: number,
      time?: number,
    ) => {
      contexts.current.add(context);
      if (mode !== "none")
        return drawSelectedTestPatternFrame(mode, context, width, height, time);
      return undefined;
    },
    [mode],
  );
  return useMemo(
    () => ({ mode, enabled: mode !== "none", drawFrame }),
    [mode, drawFrame],
  );
}
