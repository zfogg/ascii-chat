import ReactDOM from "react-dom/client";
import { HelmetProvider } from "react-helmet-async";
import { HeadingProvider } from "@ascii-chat/shared/components";
import { App } from "./App";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <HelmetProvider>
    <HeadingProvider>
      <App />
    </HeadingProvider>
  </HelmetProvider>,
);
