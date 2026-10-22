import { lazy, Suspense, useEffect } from "react";
import { Routes, Route, useLocation } from "react-router-dom";
import { Layout } from "./components/Layout";
import { SessionContent } from "./components/SessionContent";
import ErrorBoundary from "@ascii-chat/shared/components/ErrorBoundary";
import { ErrorOverlay } from "./components/ErrorOverlay";
import { installJavaScriptErrorForwarding } from "./errors/errorBus";
const HomePage = lazy(() =>
  import("./pages/Home").then((m) => ({ default: m.HomePage })),
);
const MirrorPage = lazy(() =>
  import("./pages/Mirror").then((m) => ({ default: m.MirrorPage })),
);
const ClientPage = lazy(() =>
  import("./pages/Client").then((m) => ({ default: m.ClientPage })),
);
const DiscoveryPage = lazy(() =>
  import("./pages/Discovery").then((m) => ({ default: m.DiscoveryPage })),
);
const NotFoundPage = lazy(() =>
  import("./pages/NotFound").then((m) => ({ default: m.NotFoundPage })),
);
/** Render lazy routes with shared loading and pathname-reset error recovery. */
export function App() {
  const { pathname } = useLocation();
  useEffect(() => installJavaScriptErrorForwarding(), []);
  return (
    <Layout>
      <ErrorOverlay />
      <ErrorBoundary
        key={pathname}
        fallback={<SessionContent error="Page loading failed" />}
      >
        <Suspense fallback={<SessionContent title="Opening your workspace" />}>
          <Routes>
            <Route path="/" element={<HomePage />} />
            <Route path="/mirror" element={<MirrorPage />} />
            <Route path="/client" element={<ClientPage />} />
            <Route path="/discovery" element={<DiscoveryPage />} />
            <Route path="*" element={<NotFoundPage />} />
          </Routes>
        </Suspense>
      </ErrorBoundary>
    </Layout>
  );
}
