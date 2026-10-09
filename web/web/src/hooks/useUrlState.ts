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
  const fallback = useRef<{ value: T } | null>(null);
  if (!fallback.current)
    fallback.current = {
      value: initial instanceof Function ? initial() : initial,
    };
  const [value, setValue] = useState(() =>
    readUrlValue(key, fallback.current!.value, fragment),
  );
  const current = useRef(value);
  current.current = value;
  const update = useCallback(
    (action: SetStateAction<T>) => {
      const next =
        action instanceof Function ? action(current.current) : action;
      current.current = next;
      writeUrlValue(key, next, fragment, fallback.current!.value);
      setValue(next);
    },
    [key, fragment],
  );
  useEffect(() => {
    migrateLegacyUrlState();
    writeUrlValue(key, current.current, fragment, fallback.current!.value);
    const restore = () => {
      const next = readUrlValue(key, fallback.current!.value, fragment);
      current.current = next;
      setValue(next);
    };
    window.addEventListener("popstate", restore);
    window.addEventListener("hashchange", restore);
    return () => {
      window.removeEventListener("popstate", restore);
      window.removeEventListener("hashchange", restore);
    };
  }, [key, fragment]);
  return [value, update] as const;
}
