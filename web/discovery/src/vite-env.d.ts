/// <reference types="vite-plus/client" />

declare const __COMMIT_SHA__: string;

interface ImportMetaEnv {
  readonly VITE_SSH_PUBLIC_KEY?: string;
  readonly VITE_GPG_PUBLIC_KEY?: string;
  readonly VITE_DISCOVERY_STATUS_WEBRTC_SERVERS?: string;
  readonly VITE_DISCOVERY_STATUS_STUN_SERVERS?: string;
  readonly VITE_DISCOVERY_STATUS_TURN_SERVERS?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}
