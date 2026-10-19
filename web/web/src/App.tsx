import { lazy, Suspense } from "react";
import { Routes, Route } from "react-router-dom";
import { Layout } from "./components/Layout";
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
export function App() {
  return (
    <Layout>
      <Suspense fallback={<div role="status">Loading…</div>}>
        <Routes>
          <Route path="/" element={<HomePage />} />
          <Route path="/mirror" element={<MirrorPage />} />
          <Route path="/client" element={<ClientPage />} />
          <Route path="/discovery" element={<DiscoveryPage />} />
          <Route path="*" element={<NotFoundPage />} />
        </Routes>
      </Suspense>
    </Layout>
  );
}
