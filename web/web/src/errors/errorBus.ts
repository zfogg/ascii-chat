export type BrowserErrorSource = "C/WASM" | "Protocol" | "JavaScript";

export interface BrowserErrorFrame {
  code?: number;
  message: string;
  file?: string;
  function?: string;
  line?: number;
}

export interface BrowserErrorReport {
  id: number;
  source: BrowserErrorSource;
  title: string;
  frames: BrowserErrorFrame[];
  dismiss?: () => void;
}

const EVENT = "ascii-chat:error";
let nextId = 1;
let networkReporter: ((report: BrowserErrorReport) => void) | null = null;

export function publishBrowserError(
  report: Omit<BrowserErrorReport, "id">,
  sendToServer = false,
): BrowserErrorReport {
  const published = { ...report, id: nextId++ };
  window.dispatchEvent(new CustomEvent(EVENT, { detail: published }));
  if (sendToServer) networkReporter?.(published);
  return published;
}

export function subscribeBrowserErrors(
  listener: (report: BrowserErrorReport) => void,
): () => void {
  const handler = (event: Event) =>
    listener((event as CustomEvent<BrowserErrorReport>).detail);
  window.addEventListener(EVENT, handler);
  return () => window.removeEventListener(EVENT, handler);
}

export function setNetworkErrorReporter(
  reporter: ((report: BrowserErrorReport) => void) | null,
): void {
  networkReporter = reporter;
}

function errorFrames(cause: unknown): BrowserErrorFrame[] {
  const error = cause instanceof Error ? cause : new Error(String(cause));
  const lines = error.stack?.split("\n").filter(Boolean) ?? [];
  return lines.length
    ? lines.map((line, index) => ({
        message: index === 0 ? error.message : line.trim(),
      }))
    : [{ message: error.message }];
}

function javaScriptReport(cause: unknown): Omit<BrowserErrorReport, "id"> {
  const error = cause instanceof Error ? cause : new Error(String(cause));
  return {
    source: "JavaScript",
    title: error.name || "JavaScript error",
    frames: errorFrames(error),
  };
}

export function reportJavaScriptError(cause: unknown): BrowserErrorReport {
  return publishBrowserError(javaScriptReport(cause), true);
}

export function forwardJavaScriptError(cause: unknown): BrowserErrorReport {
  const report = { ...javaScriptReport(cause), id: nextId++ };
  networkReporter?.(report);
  return report;
}

export function installJavaScriptErrorForwarding(): () => void {
  const onError = (event: ErrorEvent) =>
    reportJavaScriptError(event.error ?? event.message);
  const onRejection = (event: PromiseRejectionEvent) =>
    reportJavaScriptError(event.reason);
  window.addEventListener("error", onError);
  window.addEventListener("unhandledrejection", onRejection);
  return () => {
    window.removeEventListener("error", onError);
    window.removeEventListener("unhandledrejection", onRejection);
  };
}

export function reportProtocolError(
  code: number,
  message: string,
): BrowserErrorReport {
  const lines = message.split("\n").filter(Boolean);
  return publishBrowserError({
    source: "Protocol",
    title: `Remote protocol error ${code}`,
    frames: (lines.length ? lines : [message]).map((line) => ({
      message: line,
    })),
  });
}

export function formatReportForProtocol(report: BrowserErrorReport): string {
  return report.frames
    .map((frame) => {
      const location = frame.file
        ? ` (${frame.file}:${frame.line ?? 0}${frame.function ? ` ${frame.function}` : ""})`
        : "";
      return `${frame.message}${location}`;
    })
    .join("\n")
    .slice(0, 4096);
}
