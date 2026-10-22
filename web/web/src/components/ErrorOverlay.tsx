import { useEffect, useState } from "react";
import {
  subscribeBrowserErrors,
  type BrowserErrorReport,
} from "../errors/errorBus";

export function ErrorOverlay() {
  const [reports, setReports] = useState<BrowserErrorReport[]>([]);

  useEffect(
    () =>
      subscribeBrowserErrors((report) => setReports((old) => [...old, report])),
    [],
  );

  const report = reports[0];
  if (!report) return null;
  return (
    <section
      role="alertdialog"
      aria-label="ascii-chat error"
      className="fixed inset-0 z-[100] flex items-center justify-center bg-black/70 p-4"
    >
      <div className="max-h-[80vh] w-full max-w-4xl overflow-auto border border-red-500 bg-terminal-bg p-5 shadow-2xl">
        <div className="mb-4 flex items-start justify-between gap-4">
          <div>
            <p className="font-mono text-xs uppercase tracking-[0.2em] text-red-400">
              {report.source}
            </p>
            <h2 className="font-mono text-lg text-terminal-fg">
              {report.title}
            </h2>
          </div>
          <button
            type="button"
            className="border border-red-500 px-3 py-1 font-mono text-sm text-red-300 hover:bg-red-950"
            onClick={() => {
              report.dismiss?.();
              setReports((old) => old.slice(1));
            }}
          >
            Dismiss{reports.length > 1 ? ` (${reports.length})` : ""}
          </button>
        </div>
        <ol className="space-y-2 font-mono text-sm">
          {report.frames.map((frame, index) => (
            <li
              key={`${report.id}-${index}`}
              className="border-l-2 border-red-800 pl-3"
            >
              <div className="text-red-100">{frame.message}</div>
              {(frame.file || frame.function || frame.line) && (
                <div className="break-all text-xs text-red-400">
                  {frame.file || "<unknown>"}:{frame.line ?? 0}
                  {frame.function ? ` in ${frame.function}()` : ""}
                  {frame.code !== undefined ? ` [code ${frame.code}]` : ""}
                </div>
              )}
            </li>
          ))}
        </ol>
      </div>
    </section>
  );
}
