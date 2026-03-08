import { createContext, useContext, useRef, type ReactNode } from "react";

interface HeadingContextType {
  registerHeading: (baseId: string) => string;
}

const HeadingContext = createContext<HeadingContextType | null>(null);

export function HeadingProvider({ children }: { children: ReactNode }) {
  const usedIdsRef = useRef<Set<string>>(new Set());

  const registerHeading = (baseId: string): string => {
    if (!usedIdsRef.current.has(baseId)) {
      usedIdsRef.current.add(baseId);
      return baseId;
    }

    // Find the next available ID with a counter (only add suffix if duplicate)
    let counter = 2;
    let candidateId = `${baseId}-${counter}`;
    while (usedIdsRef.current.has(candidateId)) {
      counter++;
      candidateId = `${baseId}-${counter}`;
    }

    usedIdsRef.current.add(candidateId);
    return candidateId;
  };

  return (
    <HeadingContext.Provider value={{ registerHeading }}>
      {children}
    </HeadingContext.Provider>
  );
}

export function useHeadingContext(): HeadingContextType {
  const context = useContext(HeadingContext);
  if (!context) {
    throw new Error("useHeadingContext must be used within a HeadingProvider");
  }
  return context;
}
