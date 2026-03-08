import ReactDOM from "react-dom/client";
import { HelmetProvider } from "react-helmet-async";
import { App } from "./App";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <HelmetProvider>
    <App />
  </HelmetProvider>,
);
