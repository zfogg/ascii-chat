import { BrowserRouter, Routes, Route } from "react-router-dom";
import { HelmetProvider } from "react-helmet-async";
import { HeadingProvider } from "@ascii-chat/shared/components";
import Navigation from "./components/Navigation";
import Home from "./pages/Home";
import Crypto from "./pages/docs/Crypto";
import Man1 from "./pages/Man1";
import Man5 from "./pages/Man5";
import Man3 from "./pages/Man3";
import NotFound from "./pages/NotFound";
import DocsHub from "./pages/docs/DocsHub";
import Configuration from "./pages/docs/Configuration";
import Hardware from "./pages/docs/Hardware";
import Terminal from "./pages/docs/Terminal";
import Snapshot from "./pages/docs/Snapshot";
import Network from "./pages/docs/Network";
import Media from "./pages/docs/Media";

export default function App() {
  return (
    <BrowserRouter>
      <HelmetProvider>
        <HeadingProvider>
          <div className="flex flex-col min-h-screen overflow-y-auto">
            <Navigation />
            <div>
              <Routes>
                <Route path="/" element={<Home />} />
                <Route path="/docs" element={<DocsHub />} />
                <Route path="/docs/" element={<DocsHub />} />
                <Route path="/docs/configuration" element={<Configuration />} />
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
            </div>
          </div>
        </HeadingProvider>
      </HelmetProvider>
    </BrowserRouter>
  );
}
