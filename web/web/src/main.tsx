import ReactDOM from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import { ErrorBoundary, HeadingProvider } from "@ascii-chat/shared/components";
import { App } from "./App";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <ErrorBoundary>
    <BrowserRouter>
      <HeadingProvider>
        <App />
      </HeadingProvider>
    </BrowserRouter>
  </ErrorBoundary>,
);
