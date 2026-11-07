import { useContext } from "react";
import { UNSAFE_LocationContext } from "react-router-dom";

export function useSafeLocation() {
  const context = useContext(UNSAFE_LocationContext);
  return context?.location ?? { pathname: "/" };
}
