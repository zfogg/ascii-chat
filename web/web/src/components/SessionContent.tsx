import type { ReactNode } from "react";

/** Reserve the actual rendering box through page and engine startup. */
export function SessionContent({
  children,
  loading = true,
  error,
  title = "Preparing your view",
}: {
  children?: ReactNode;
  loading?: boolean;
  error?: string | undefined;
  title?: string;
}) {
  const pending = loading || !!error;
  return (
    <section
      className="session-content-box"
      aria-label="ASCII view"
      aria-busy={loading && !error}
    >
      {children && (
        <div
          className="session-content-view"
          style={{ visibility: pending ? "hidden" : "visible" }}
          aria-hidden={pending || undefined}
        >
          {children}
        </div>
      )}
      {pending && (
        <div className="session-loading" data-testid="session-loading">
          <div
            className="session-loading-message"
            role={error ? "alert" : "status"}
            aria-live={error ? "assertive" : "polite"}
            aria-atomic="true"
          >
            <div className="session-loading-mark" aria-hidden="true">
              <span>[</span>
              <span className={error ? "" : "session-loading-cursor"}>
                {error ? "!" : "▌"}
              </span>
              <span>]</span>
            </div>
            <h2>{error ? "The view couldn’t load" : title}</h2>
            <p>
              {error
                ? "Check your connection, then reload this page to try again."
                : "Getting everything ready for your next frame."}
            </p>
            {error && (
              <button type="button" onClick={() => window.location.reload()}>
                Reload page
              </button>
            )}
          </div>
        </div>
      )}
    </section>
  );
}
