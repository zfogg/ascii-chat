/// <reference types="vite-plus/client" />

declare const __COMMIT_SHA__: string;

interface ImportMetaEnv {
  readonly VITE_DISCOVERY_API_BASE?: string;
  readonly VITE_SSH_PUBLIC_KEY?: string;
  readonly VITE_GPG_PUBLIC_KEY?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
