import { createContext, useContext, useRef, useEffect, useCallback, useMemo, type ReactNode } from "react";

interface HeadingContextType {
  registerHeading: (baseId: string) => string;
}

const HeadingContext = createContext<HeadingContextType | null>(null);

export function HeadingProvider({ children }: { children: ReactNode }) {
  const usedIdsRef = useRef<Set<string>>(new Set());

  // Reset heading IDs when page changes
  useEffect(() => {
    const handlePopstate = () => {
      usedIdsRef.current.clear();
    };
    window.addEventListener("popstate", handlePopstate);
    return () => window.removeEventListener("popstate", handlePopstate);
  }, []);

  const registerHeading = useCallback((baseId: string): string => {
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
  }, []);

  const contextValue = useMemo(() => ({ registerHeading }), [registerHeading]);

  return (
    <HeadingContext.Provider value={contextValue}>
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
