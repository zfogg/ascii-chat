import { useLocation as useReactLocation } from "react-router-dom";

export function useSafeLocation() {
  try {
    return useReactLocation();
  } catch {
    // Fallback if not in Router context
    return { pathname: "/" } as ReturnType<typeof useReactLocation>;
  }
}
