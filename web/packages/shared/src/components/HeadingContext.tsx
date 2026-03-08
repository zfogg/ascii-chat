import { createContext, useContext, useCallback, useState, type ReactNode } from "react";

interface HeadingContextType {
  registerHeading: (baseId: string) => string;
  resetHeadings: () => void;
}

const HeadingContext = createContext<HeadingContextType | null>(null);

export function HeadingProvider({ children }: { children: ReactNode }) {
  const [usedIds, setUsedIds] = useState<Set<string>>(new Set());

  const registerHeading = useCallback((baseId: string): string => {
    if (!usedIds.has(baseId)) {
      setUsedIds((prev) => new Set([...prev, baseId]));
      return baseId;
    }

    // Find the next available ID with a counter
    let counter = 2;
    let candidateId = `${baseId}-${counter}`;
    while (usedIds.has(candidateId)) {
      counter++;
      candidateId = `${baseId}-${counter}`;
    }

    setUsedIds((prev) => new Set([...prev, candidateId]));
    return candidateId;
  }, [usedIds]);

  const resetHeadings = useCallback(() => {
    setUsedIds(new Set());
  }, []);

  return (
    <HeadingContext.Provider value={{ registerHeading, resetHeadings }}>
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
