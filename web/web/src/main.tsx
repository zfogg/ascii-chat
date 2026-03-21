import ReactDOM from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import { HelmetProvider } from "react-helmet-async";
import { ErrorBoundary, HeadingProvider } from "@ascii-chat/shared/components";
import { App } from "./App";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <ErrorBoundary>
    <BrowserRouter>
      <HelmetProvider>
        <HeadingProvider>
          <App />
        </HeadingProvider>
      </HelmetProvider>
    </BrowserRouter>
  </ErrorBoundary>,
);
