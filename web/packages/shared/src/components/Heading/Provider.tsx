import { useRef, useEffect, useCallback, useMemo, type ReactNode } from "react";
import { useLocation } from "react-router-dom";
import { HeadingContext } from "./context";

export function HeadingProvider({ children }: { children: ReactNode }) {
  const usedIdsRef = useRef<Set<string>>(new Set());
  const location = useLocation();

  // Reset heading IDs when page changes
  useEffect(() => {
    usedIdsRef.current.clear();
  }, [location.pathname]);

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
