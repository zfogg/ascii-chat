import { createContext } from "react";

export interface HeadingContextType {
  registerHeading: (baseId: string) => string;
  unregisterHeading: (id: string) => void;
}

export const HeadingContext = createContext<HeadingContextType | null>(null);
