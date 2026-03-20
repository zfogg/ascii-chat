import { Routes, Route } from "react-router-dom";
import {
  HomePage,
  MirrorPage,
  NotFoundPage,
  ClientPage,
  DiscoveryPage,
} from "./pages";
import { Layout } from "./components";

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
