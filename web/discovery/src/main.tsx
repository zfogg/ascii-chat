import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { BrowserRouter, Routes, Route } from "react-router-dom";
import { ErrorBoundary, HeadingProvider } from "@ascii-chat/shared/components";
import "./index.css";
import Home from "./pages/Home";
import NotFound from "./pages/NotFound";

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <ErrorBoundary>
      <BrowserRouter>
        <HeadingProvider>
          <Routes>
            <Route path="/" element={<Home />} />
            <Route path="*" element={<NotFound />} />
          </Routes>
        </HeadingProvider>
      </BrowserRouter>
    </ErrorBoundary>
  </StrictMode>,
);
