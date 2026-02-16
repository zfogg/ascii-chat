import { HomePage } from "./pages/Home";
import { MirrorPage } from "./pages/Mirror";
import { NotFoundPage } from "./pages/NotFound";
import { ClientPage } from "./pages/Client";
import { DiscoveryPage } from "./pages/Discovery";
import { Layout } from "./components/Layout";

export function App() {
  const path = window.location.pathname;

  // Determine which page to render
  let page;
  if (path === "/" || path === "") {
    page = <HomePage />;
  } else if (path === "/mirror" || path === "/mirror/") {
    page = <MirrorPage />;
  } else if (path === "/client" || path === "/client/") {
    page = <ClientPage />;
  } else if (path === "/discovery" || path === "/discovery/") {
    page = <DiscoveryPage />;
  } else {
    page = <NotFoundPage />;
  }

  return <Layout>{page}</Layout>;
}
