import { Routes, Route } from "react-router-dom";
import { HomePage } from "./pages/Home";
import { MirrorPage } from "./pages/Mirror";
import { NotFoundPage } from "./pages/NotFound";
import { ClientPage } from "./pages/Client";
import { DiscoveryPage } from "./pages/Discovery";
import { Layout } from "./components/Layout";

export function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<HomePage />} />
        <Route path="/mirror" element={<MirrorPage />} />
        <Route path="/client" element={<ClientPage />} />
        <Route path="/discovery" element={<DiscoveryPage />} />
        <Route path="*" element={<NotFoundPage />} />
      </Routes>
    </Layout>
  );
}
