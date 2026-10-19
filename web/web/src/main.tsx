import ReactDOM from "react-dom/client";
import { BrowserRouter } from "react-router-dom";
import ErrorBoundary from "@ascii-chat/shared/components/ErrorBoundary";
import { HeadingProvider } from "@ascii-chat/shared/components/Heading";
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
