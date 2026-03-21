import { ReactNode, Component, ErrorInfo } from "react";

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
}

interface State {
  hasError: boolean;
  error?: Error;
}

export default class ErrorBoundary extends Component<Props, State> {
  static displayName = "ErrorBoundary";

  constructor(props: Props) {
    super(props);
    this.state = { hasError: false };
  }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  componentDidCatch(error: Error, errorInfo: ErrorInfo) {
    console.error("Error caught by boundary:", error, errorInfo);
    // Track in analytics if available
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const w = window as any;
    if (typeof window !== "undefined" && w.gtag) {
      w.gtag("event", "exception", {
        description: `${error.name}: ${error.message}`,
        fatal: "true",
      });
    }
  }

  render() {
    if (this.state.hasError) {
      if (this.props.fallback) {
        return this.props.fallback;
      }

      return (
        <div className="flex flex-col h-screen bg-gray-950 text-gray-100">
          <main className="flex-1 flex items-center justify-center">
            <div className="max-w-md text-center px-4">
              <h1 className="text-3xl font-bold mb-4 text-red-400">
                Something went wrong
              </h1>
              <p className="text-gray-300 mb-6">
                We encountered an unexpected error. Please try refreshing the
                page.
              </p>
              <button
                onClick={() => {
                  window.location.href = "/";
                }}
                className="inline-block px-6 py-2 bg-fuchsia-600 hover:bg-fuchsia-700 rounded transition-colors font-semibold"
              >
                Go Home
              </button>
              {typeof window !== "undefined" &&
                window.location.hostname === "localhost" &&
                this.state.error && (
                  <details className="mt-6 text-left">
                    <summary className="cursor-pointer text-gray-400 hover:text-gray-300">
                      Debug Info (dev only)
                    </summary>
                    <pre className="mt-2 p-3 bg-gray-900 rounded text-xs overflow-auto max-h-40">
                      {this.state.error.toString()}
                    </pre>
                  </details>
                )}
            </div>
          </main>
        </div>
      );
    }

    return this.props.children;
  }
}
