import { useEffect, useState } from "react";
import {
  sourceUrlForFrame,
  subscribeBrowserErrors,
  type BrowserErrorFrame,
  type BrowserErrorReport,
} from "../errors/errorBus";

function FramePath({
  frame,
  productionMode,
}: {
  frame: BrowserErrorFrame;
  productionMode: boolean;
}) {
  const file = frame.file || "<unknown>";
  const label = `${file}:${frame.line ?? 0}`;
  const interactiveClassName =
    "text-blue-400 underline decoration-blue-500/70 underline-offset-2 transition-colors hover:text-blue-300 hover:decoration-blue-300 active:text-purple-300";
  const sourceUrl = productionMode ? sourceUrlForFrame(frame) : null;
  if (sourceUrl) {
    const revision = __COMMIT_SHA__ === "unknown" ? "master" : __COMMIT_SHA__;
    return (
      <a
        href={sourceUrl}
        target="_blank"
        rel="noopener noreferrer"
        className={interactiveClassName}
        title={`Open ${label} on GitHub at ${revision}`}
        aria-label={`Open ${file} line ${frame.line} on GitHub at ${revision}`}
      >
        {label}
      </a>
    );
  }
  if (!productionMode && frame.fullFile) {
    return (
      <button
        type="button"
        className={interactiveClassName}
        title={`Copy full path: ${frame.fullFile}`}
        aria-label={`Copy full path for ${file}`}
        onClick={() => void navigator.clipboard.writeText(frame.fullFile!)}
      >
        {label}
      </button>
    );
  }
  return <span className="text-blue-400">{label}</span>;
}

export function ErrorOverlay({
  productionMode = import.meta.env.PROD,
}: {
  productionMode?: boolean;
}) {
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
        <p className="mb-2 font-mono text-xs text-terminal-7">
          Stack order: 0 is newest
        </p>
        <ol className="space-y-4 font-mono text-sm">
          {report.frames.map((frame, index) => (
            <li
              key={`${report.id}-${index}`}
              className="grid grid-cols-[2rem_1fr] gap-x-3 border border-red-900 bg-red-950/15 px-3 py-2"
            >
              <span
                className="row-span-2 self-center border border-red-800 py-1 text-center text-xs text-red-300"
                title={index === 0 ? "Newest error" : `Stack frame ${index}`}
              >
                {index}
              </span>
              <div>
                {(frame.file || frame.function || frame.line) && (
                  <div className="mb-1 break-all text-xs">
                    <FramePath frame={frame} productionMode={productionMode} />
                    {frame.function && (
                      <>
                        <span className="text-terminal-fg"> in </span>
                        <span className="text-orange-400">
                          {frame.function}()
                        </span>
                      </>
                    )}
                    {frame.code !== undefined && (
                      <span className="text-red-400"> [code {frame.code}]</span>
                    )}
                  </div>
                )}
                <div className="text-red-100">{frame.message}</div>
              </div>
            </li>
          ))}
        </ol>
      </div>
    </section>
  );
}
