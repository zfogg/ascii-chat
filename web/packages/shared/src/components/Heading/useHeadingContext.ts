import { useContext } from "react";
import { HeadingContext, type HeadingContextType } from "./context";

export function useHeadingContext(): HeadingContextType {
  const context = useContext(HeadingContext);
  if (!context) {
    throw new Error("useHeadingContext must be used within a HeadingProvider");
  }
  return context;
}
