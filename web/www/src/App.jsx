import { lazy, Suspense } from "react";
import { BrowserRouter, Routes, Route } from "react-router-dom";
import { HelmetProvider } from "react-helmet-async";
import { HeadingProvider } from "@ascii-chat/shared/components";
import Navigation from "./components/Navigation";
import Home from "./pages/Home";

const Crypto = lazy(() => import("./pages/docs/Crypto"));
const Man1 = lazy(() => import("./pages/Man1"));
const Man5 = lazy(() => import("./pages/Man5"));
const Man3 = lazy(() => import("./pages/man3"));
const NotFound = lazy(() => import("./pages/NotFound"));
const DocsHub = lazy(() => import("./pages/docs/DocsHub"));
const Configuration = lazy(() => import("./pages/docs/Configuration"));
const Hardware = lazy(() => import("./pages/docs/Hardware"));
const Terminal = lazy(() => import("./pages/docs/Terminal"));
const Snapshot = lazy(() => import("./pages/docs/Snapshot"));
const Network = lazy(() => import("./pages/docs/Network"));
const Media = lazy(() => import("./pages/docs/Media"));

export default function App() {
  return (
    <BrowserRouter>
      <HelmetProvider>
        <HeadingProvider>
          <div className="flex flex-col overflow-y-auto">
            <Navigation />
            <main className="pt-[65px] flex flex-col flex-1">
              <Suspense fallback={null}>
                <Routes className="flex-1">
                  <Route path="/" element={<Home />} />
                  <Route path="/docs" element={<DocsHub />} />
                  <Route path="/docs/" element={<DocsHub />} />
                  <Route
                    path="/docs/configuration"
                    element={<Configuration />}
                  />
                  <Route path="/docs/hardware" element={<Hardware />} />
                  <Route path="/docs/terminal" element={<Terminal />} />
                  <Route path="/docs/snapshot" element={<Snapshot />} />
                  <Route path="/docs/network" element={<Network />} />
                  <Route path="/docs/media" element={<Media />} />
                  <Route path="/docs/crypto" element={<Crypto />} />
                  <Route path="/man1" element={<Man1 />} />
                  <Route path="/man5" element={<Man5 />} />
                  <Route path="/man3" element={<Man3 />} />
                  <Route path="*" element={<NotFound />} />
                </Routes>
              </Suspense>
            </main>
          </div>
        </HeadingProvider>
      </HelmetProvider>
    </BrowserRouter>
  );
}
