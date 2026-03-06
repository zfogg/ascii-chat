import ReactDOM from "react-dom/client";
import { StrictMode } from "react";
import { HelmetProvider } from "react-helmet-async";
import { NotFoundPage } from "./pages/NotFound";
import { Layout } from "./components/Layout";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <StrictMode>
    <HelmetProvider>
      <Layout>
        <NotFoundPage />
      </Layout>
    </HelmetProvider>
  </StrictMode>,
);
