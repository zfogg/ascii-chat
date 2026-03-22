import ReactDOM from "react-dom/client";
import { StrictMode } from "react";
import { NotFoundPage } from "./pages";
import { Layout } from "./components";
import "./style.css";

ReactDOM.createRoot(document.getElementById("app")!).render(
  <StrictMode>
    <Layout>
      <NotFoundPage />
    </Layout>
  </StrictMode>,
);
