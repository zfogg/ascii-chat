type StopFn = () => void;

let activeStop: StopFn | null = null;

export function registerActiveDemo(stop: StopFn): void {
  if (activeStop && activeStop !== stop) {
    activeStop();
  }
  activeStop = stop;
}

export function unregisterActiveDemo(stop: StopFn): void {
  if (activeStop === stop) {
    activeStop = null;
  }
}
