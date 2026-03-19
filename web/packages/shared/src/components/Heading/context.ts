import { createContext } from "react";

export interface HeadingContextType {
  registerHeading: (baseId: string) => string;
}

export const HeadingContext = createContext<HeadingContextType | null>(null);
