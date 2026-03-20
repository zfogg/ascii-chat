declare global {
  interface Window {
    gtag?: (
      command: string,
      eventName: string,
      params?: Record<string, unknown>,
    ) => void;
  }

  // Build-time injected globals
  const __COMMIT_SHA__: string;
}

export {};
