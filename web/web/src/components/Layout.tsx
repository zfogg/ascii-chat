import { ReactNode } from "react";
import { Header } from "./Header";
import { Footer } from "./Footer";
import { useInitializeWasm } from "../hooks";

interface LayoutProps {
  children: ReactNode;
}

export function Layout({ children }: LayoutProps) {
  useInitializeWasm();

  return (
    <div className="flex flex-col">
      <div className="min-h-dvh flex flex-col">
        <Header />
        <div className="flex-1 min-h-0 flex flex-col">{children}</div>
      </div>
      <Footer />
    </div>
  );
}
