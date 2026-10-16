import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type SetStateAction,
} from "react";
import {
  migrateLegacyUrlState,
  readUrlValue,
  writeUrlValue,
} from "../utils/urlState";

export function useUrlState<T>(
  key: string,
  initial: T | (() => T),
  fragment = false,
) {
  const [fallback] = useState(() =>
    initial instanceof Function ? initial() : initial,
  );
  const [value, setValue] = useState(() =>
    readUrlValue(key, fallback, fragment),
  );
  const current = useRef(value);
  const update = useCallback(
    (action: SetStateAction<T>) => {
      const next =
        action instanceof Function ? action(current.current) : action;
      current.current = next;
      writeUrlValue(key, next, fragment, fallback);
      setValue(next);
    },
    [key, fragment, fallback],
  );
  useEffect(() => {
    migrateLegacyUrlState();
    writeUrlValue(key, current.current, fragment, fallback);
    const restore = () => {
      const next = readUrlValue(key, fallback, fragment);
      current.current = next;
      setValue(next);
    };
    window.addEventListener("popstate", restore);
    window.addEventListener("hashchange", restore);
    return () => {
      window.removeEventListener("popstate", restore);
      window.removeEventListener("hashchange", restore);
    };
  }, [key, fragment, fallback]);
  return [value, update] as const;
}
