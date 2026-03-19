/// <reference types="vite-plus/client" />

declare const __COMMIT_SHA__: string;

interface ImportMetaEnv {
  readonly VITE_DISCOVERY_API_BASE?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
