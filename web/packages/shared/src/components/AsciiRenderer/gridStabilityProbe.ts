/** Optional one-minute pixel/layout probe for a live green test-pattern peer. */
export function startGridStabilityProbe(canvas: HTMLCanvasElement): void {
  if (canvas.dataset["gridProbe"]) return;
  const started = performance.now() + 10_000;
  const result = {
    samples: 0,
    minCanvasTop: Infinity,
    maxCanvasTop: -Infinity,
    minPatternTop: Infinity,
    maxPatternTop: -Infinity,
    complete: false,
  };
  canvas.dataset["gridProbe"] = JSON.stringify(result);
  const timer = setInterval(() => {
    if (!canvas.isConnected) {
      clearInterval(timer);
      return;
    }
    if (performance.now() < started) return;
    const top = canvas.getBoundingClientRect().top;
    result.minCanvasTop = Math.min(result.minCanvasTop, top);
    result.maxCanvasTop = Math.max(result.maxCanvasTop, top);
    const context = canvas.getContext("2d");
    if (context) {
      const pixels = context.getImageData(
        0,
        0,
        canvas.width,
        canvas.height,
      ).data;
      let patternTop = -1;
      for (let y = 0; y < canvas.height && patternTop < 0; y++) {
        for (let x = 0; x < canvas.width; x += 2) {
          const offset = (y * canvas.width + x) * 4;
          if (
            pixels[offset]! < 30 &&
            pixels[offset + 1]! > 180 &&
            pixels[offset + 2]! < 30
          ) {
            patternTop = y;
            break;
          }
        }
      }
      if (patternTop >= 0) {
        result.minPatternTop = Math.min(result.minPatternTop, patternTop);
        result.maxPatternTop = Math.max(result.maxPatternTop, patternTop);
        result.samples++;
      }
    }
    result.complete = performance.now() - started >= 60_000;
    canvas.dataset["gridProbe"] = JSON.stringify(result);
    if (result.complete) clearInterval(timer);
  }, 250);
}
