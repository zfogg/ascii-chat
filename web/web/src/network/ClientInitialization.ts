// The WASM client context is shared by browser transports. A cancelled setup
// must finish before the next session configures keys or installs callbacks.
let pending: Promise<void> = Promise.resolve();

export function serializeClientInitialization<T>(
  prepare: () => Promise<T>,
): Promise<T> {
  const result = pending.then(prepare);
  pending = result.then(
    () => {},
    () => {},
  );
  return result;
}
