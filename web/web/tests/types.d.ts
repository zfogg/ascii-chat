/// <reference types="node" />

// Type declarations for ws module used in tests
declare module "ws" {
  import { EventEmitter } from "events";
  import { Server, ClientRequest, IncomingMessage } from "http";
  import { Socket } from "net";
  import { Duplex } from "stream";

  export type RawData = Buffer | ArrayBuffer | Buffer[];

  export interface WebSocketOptions {
    host?: string;
    port?: number;
    backlog?: number;
    server?: Server;
    verifyClient?:
      | ((
          info: { origin: string; secure: boolean; req: IncomingMessage },
          callback: (result: boolean, code?: number, message?: string) => void,
        ) => void)
      | undefined;
    handleProtocols?: (
      protocols: string[],
      request: IncomingMessage,
    ) => string | false;
    path?: string;
    noServer?: boolean;
    clientTracking?: boolean;
    perMessageDeflate?: any;
    maxPayload?: number;
    skipUTF8Validation?: boolean;
  }

  export class WebSocketServer extends EventEmitter {
    constructor(options?: WebSocketOptions, callback?: () => void);
    on(
      event: "connection",
      listener: (ws: WebSocket, request: IncomingMessage) => void,
    ): this;
    on(event: "error", listener: (error: Error) => void): this;
    on(
      event: "headers",
      listener: (headers: string[], request: IncomingMessage) => void,
    ): this;
    on(event: "close", listener: () => void): this;
    on(event: "listening", listener: () => void): this;

    once(
      event: "connection",
      listener: (ws: WebSocket, request: IncomingMessage) => void,
    ): this;
    once(event: "error", listener: (error: Error) => void): this;
    once(event: "close", listener: () => void): this;
    once(event: "listening", listener: () => void): this;

    close(cb?: (err?: Error) => void): void;
    shouldHandle(req: IncomingMessage): boolean;
    handleUpgrade(
      req: IncomingMessage,
      socket: Socket,
      upgradeHead: Buffer,
      callback: (ws: WebSocket, req: IncomingMessage) => void,
    ): void;

    clients: Set<WebSocket>;
  }

  export class WebSocket extends EventEmitter {
    constructor(
      address: string | URL,
      protocols?: string | string[],
      options?: any,
    );
    on(event: "open", listener: () => void): this;
    on(event: "message", listener: (data: RawData) => void): this;
    on(event: "close", listener: (code: number, reason: Buffer) => void): this;
    on(event: "error", listener: (error: Error) => void): this;
    on(event: "ping", listener: (data: Buffer) => void): this;
    on(event: "pong", listener: (data: Buffer) => void): this;

    send(
      data: string | Buffer | ArrayBufferLike | Iterable<number>,
      callback?: (err?: Error) => void,
    ): void;
    close(code?: number, reason?: string | Buffer): void;
    ping(data?: any, mask?: boolean, callback?: (err?: Error) => void): void;
    pong(data?: any, mask?: boolean, callback?: (err?: Error) => void): void;
    terminate(): void;

    readyState: number;
    CONNECTING: 0;
    OPEN: 1;
    CLOSING: 2;
    CLOSED: 3;

    bufferedAmount: number;
    binaryType: "nodebuffer" | "arraybuffer" | "fragments";
    url?: string;
    extensions?: Record<string, any>;
    protocol?: string;
  }
}
