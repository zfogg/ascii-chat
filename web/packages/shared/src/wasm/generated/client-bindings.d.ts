// TypeScript bindings for emscripten-generated code.  Automatically generated at compile time.
declare var RuntimeExports: {
  /** @type {!Uint8Array} */
  HEAPU8: Uint8Array;
  FS: {
    root: null;
    mounts: never[];
    devices: {};
    streams: never[];
    nextInode: number;
    nameTable: never[];
    currentPath: string;
    initialized: boolean;
    ignorePermissions: boolean;
    syncFSRequests: number;
    ErrnoError: {
      new (errno: any): {
        cause?: unknown;
        name: string;
        errno: any;
        code: string | undefined;
        message: string;
        stack?: string;
      };
    };
    FSStream: {
      new (): {
        shared: {};
        get object(): any;
        set object(val: any);
        node: any;
        get isRead(): boolean;
        get isWrite(): boolean;
        get isAppend(): number;
        flags: any;
        position: any;
      };
    };
    FSNode: {
      new (
        parent: any,
        name: any,
        mode: any,
        rdev: any,
      ): {
        node_ops: {};
        stream_ops: {};
        readMode: number;
        writeMode: number;
        mounted: null;
        parent: any;
        mount: any;
        id: number;
        name: any;
        mode: any;
        rdev: any;
        atime: number;
        mtime: number;
        ctime: number;
        get read(): boolean;
        set read(val: boolean);
        get write(): boolean;
        set write(val: boolean);
        readonly isFolder: any;
        readonly isDevice: any;
        addListener(
          cb: any,
          exclusive?: boolean,
        ): {
          listeners: any;
          entry: {
            cb: any;
            exclusive: boolean;
          };
        };
        notifyListeners(flags: any): void;
        exclTurn: any;
      };
    };
    lookupPath(
      path: any,
      opts?: {},
    ):
      | {
          path: string;
          node?: undefined;
        }
      | {
          path: string;
          node: any;
        };
    getPath(node: any): any;
    hashName(parentid: any, name: any): number;
    hashAddNode(node: any): void;
    hashRemoveNode(node: any): void;
    lookupNode(parent: any, name: any): any;
    createNode(parent: any, name: any, mode: any, rdev?: undefined): any;
    destroyNode(node: any): void;
    isRoot(node: any): boolean;
    isMountpoint(node: any): boolean;
    isFile(mode: any): boolean;
    isDir(mode: any): boolean;
    isLink(mode: any): boolean;
    isChrdev(mode: any): boolean;
    isBlkdev(mode: any): boolean;
    isFIFO(mode: any): boolean;
    isSocket(mode: any): boolean;
    flagsToPermissionString(flag: any): string;
    nodePermissions(node: any, perms: any): 0 | 2;
    mayLookup(dir: any): any;
    mayCreate(dir: any, name: any): any;
    mayDelete(dir: any, name: any, isdir: any): any;
    mayOpen(node: any, flags: any): any;
    checkOpExists(op: any, err: any): any;
    MAX_OPEN_FDS: number;
    nextfd(): number;
    getStreamChecked(fd: any): any;
    getStream: (fd: any) => any;
    createStream(stream: any, fd?: number): any;
    closeStream(fd: any): void;
    dupStream(origStream: any, fd?: number): any;
    doSetAttr(stream: any, node: any, attr: any): void;
    chrdev_stream_ops: {
      open(stream: any): void;
      llseek(): never;
    };
    major: (dev: any) => number;
    minor: (dev: any) => number;
    makedev: (ma: any, mi: any) => number;
    registerDevice(dev: any, ops: any): void;
    getDevice: (dev: any) => any;
    getMounts(mount: any): any[];
    syncfs(populate: any, callback: any): void;
    mount(type: any, opts: any, mountpoint: any): any;
    unmount(mountpoint: any): void;
    lookup(parent: any, name: any): any;
    mknod(path: any, mode: any, dev: any): any;
    statfs(path: any): any;
    statfsStream(stream: any): any;
    statfsNode(node: any): {
      bsize: number;
      frsize: number;
      blocks: number;
      bfree: number;
      bavail: number;
      files: any;
      ffree: number;
      fsid: number;
      flags: number;
      namelen: number;
    };
    create(path: any, mode?: number): any;
    mkdir(path: any, mode?: number): any;
    mkdirTree(path: any, mode?: number): void;
    mkdev(path: any, mode: any, dev?: undefined): any;
    symlink(oldpath: any, newpath: any): any;
    link(oldpath: any, newpath: any, flags: any): any;
    rename(old_path: any, new_path: any): void;
    rmdir(path: any): void;
    readdir(path: any): any;
    unlink(path: any): void;
    readlink(path: any): any;
    stat(path: any, dontFollow?: boolean): any;
    fstat(fd: any): any;
    lstat(path: any): any;
    doChmod(stream: any, node: any, mode: any, dontFollow?: boolean): void;
    chmod(path: any, mode: any, dontFollow?: boolean): void;
    lchmod(path: any, mode: any): void;
    fchmod(fd: any, mode: any): void;
    doChown(stream: any, node: any, dontFollow?: boolean): void;
    chown(path: any, uid: any, gid: any, dontFollow?: boolean): void;
    lchown(path: any, uid: any, gid: any): void;
    fchown(fd: any, uid: any, gid: any): void;
    doTruncate(stream: any, node: any, len: any): void;
    truncate(path: any, len: any): void;
    ftruncate(fd: any, len: any): void;
    utime(path: any, atime: any, mtime: any, dontFollow?: boolean): void;
    open(path: any, flags: any, mode?: number): any;
    close(stream: any): void;
    isClosed(stream: any): boolean;
    llseek(stream: any, offset: any, whence: any): any;
    read(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      position?: undefined,
    ): any;
    write(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      position?: undefined,
      canOwn?: undefined,
    ): any;
    mmap(stream: any, length: any, position: any, prot: any, flags: any): any;
    msync(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      mmapFlags: any,
    ): any;
    ioctl(stream: any, cmd: any, arg: any): any;
    readFile(path: any, opts?: {}): Uint8Array<any>;
    writeFile(path: any, data: any, opts?: {}): void;
    cwd: () => any;
    chdir(path: any): void;
    createDefaultDirectories(): void;
    createDefaultDevices(): void;
    createSpecialDirectories(): void;
    createStandardStreams(input: any, output: any, error: any): void;
    staticInit(): void;
    init(input?: undefined, output?: undefined, error?: undefined): void;
    quit(): void;
    analyzePath(
      path: any,
      dontResolveLastLink?: boolean,
    ): {
      isRoot: boolean;
      exists: boolean;
      error: number;
      name: null;
      path: null;
      object: null;
      parentExists: boolean;
      parentPath: null;
      parentObject: null;
    };
    createPath(
      parent: any,
      path: any,
      canRead?: undefined,
      canWrite?: undefined,
    ): any;
    createFile(
      parent: any,
      name: any,
      properties: any,
      canRead?: undefined,
      canWrite?: undefined,
    ): any;
    createDataFile(
      parent: any,
      name: any,
      data?: undefined,
      canRead?: undefined,
      canWrite?: undefined,
      canOwn?: undefined,
    ): void;
    createDevice(
      parent: any,
      name: any,
      input?: undefined,
      output?: undefined,
    ): any;
    forceLoadFile(obj: any): true | undefined;
    createLazyFile(
      parent: any,
      name: any,
      url: any,
      canRead?: undefined,
      canWrite?: undefined,
    ): any;
  };
};
interface WasmModule {}

type EmbindString =
  | ArrayBuffer
  | Uint8Array
  | Uint8ClampedArray
  | Int8Array
  | string;
export interface ClassHandle {
  isAliasOf(other: ClassHandle): boolean;
  delete(): void;
  deleteLater(): this;
  isDeleted(): boolean;
  // @ts-ignore - If targeting lower than ESNext, this symbol might not exist.
  [Symbol.dispose](): void;
  clone(): this;
}
export interface SessionUiBinding extends ClassHandle {
  valid(): boolean;
  resize(_0: number, _1: number): void;
  loading(_0: boolean, _1: EmbindString): void;
  log(_0: EmbindString): void;
  media(_0: EmbindString): number;
  key(_0: number): number;
  clear(): void;
  redraw(): void;
  tick(_0: number): string | undefined;
}

export type RasterPixels = Uint8ClampedArray;

export interface TerminalRendererBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  cols(): number;
  rows(): number;
  width(): number;
  height(): number;
  setZalgo(_0: number): number;
  feed(_0: EmbindString): number;
  copyPixels(_0: RasterPixels): boolean;
}

export type FacePixels = Uint8ClampedArray;

export type FaceValues = Float32Array;

export type PatternPixels = Uint8ClampedArray;

export interface TestPatternBinding extends ClassHandle {
  valid(): boolean;
  render(
    _0: number,
    _1: number,
    _2: number,
    _3: number,
    _4: boolean,
    _5: PatternPixels,
  ): boolean;
}

export type AudioSamples = Float32Array;

export type PcmSamples = Int16Array;

export type OpusBytes = Uint8Array;

export type NullableOpusBytes = Uint8Array | null;

export type NullablePcmSamples = Int16Array | null;

export interface AudioProcessorBinding extends ClassHandle {
  valid(): boolean;
  process(_0: AudioSamples, _1: AudioSamples): number;
}

export interface OpusCodecBinding extends ClassHandle {
  valid(): boolean;
  encode(_0: PcmSamples): NullableOpusBytes;
  decode(_0: OpusBytes, _1: number): NullablePcmSamples;
}

export type ClientSettingsResult = { code: number; json: string | null };

export type PeerBytes = Uint8Array;

export type NullablePeerBytes = Uint8Array | null;

export interface PeerCryptoBinding extends ClassHandle {
  valid(): boolean;
  ready(): boolean;
  error(): string;
  send(_0: PeerBytes): number;
  receive(_0: PeerBytes): number;
  tick(): number;
  beginQuic(): number;
  initQuic(): number;
  tickQuic(): number;
  quicEpoch(): number;
  takeOutput(): NullablePeerBytes;
  received(): NullablePeerBytes;
  takeQuicOutput(): QuicControlResult;
  quicPacket(
    _0: boolean,
    _1: number,
    _2: number,
    _3: PeerBytes,
  ): QuicPacketResult;
}

export interface ClientSessionBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
}

export type QuicControlResult = {
  code: number;
  flow: number;
  packet: Uint8Array | null;
};

export type QuicPacketResult = {
  code: number;
  sequence: bigint;
  packet: Uint8Array;
};

export type KeyInput = Uint8Array;

export type KeyParseResult = {
  code: number;
  bytes: Uint8Array;
  frames: ErrorFrameResult[];
};

export type ErrorFrameResult = {
  code: number;
  message: string;
  file: string;
  fullFile: string;
  function: string;
  line: number;
};

export interface AVStateBinding extends ClassHandle {
  valid(): boolean;
  generation(): number;
  begin(): number;
  complete(_0: number): boolean;
  current(_0: number): boolean;
  active(): boolean;
  beginStop(): void;
  finishStop(): void;
}

export type ClientBytes = Uint8Array;

export type ClientPacketResult = { code: number; bytes: Uint8Array };

export type ClientFrameResult = {
  code: number;
  decodeFailed: boolean;
  bytes: Uint8Array;
};

export type DiscoverySnapshotValue = {
  state: number;
  terminal: boolean;
  error: string;
  name: string;
  joined: {
    participantId: Uint8Array;
    sessionId: Uint8Array;
    hostId: Uint8Array;
    turnUsername: string;
    turnPassword: string;
    serverAddress: string;
    serverPort: number;
    sessionType: number;
    quicPort: number;
    webtransportUrl: string;
  };
};

export type HandoffActionValue = {
  kind: number;
  participant: Uint8Array;
  generation: number;
  sequence: number;
} | null;

export type ProtocolBytes = Uint8Array;

export type OptionalHandoffBytes = Uint8Array | null;

export interface DiscoveryBinding extends ClassHandle {
  valid(): boolean;
  callbackToken(): number;
  enter(_0: boolean, _1: EmbindString, _2: ProtocolBytes): number;
  receive(_0: number, _1: ProtocolBytes): number;
  leave(): number;
  snapshot(): DiscoverySnapshotValue;
}

export interface HostPacketBinding extends ClassHandle {
  receive(_0: number, _1: number, _2: ProtocolBytes): number;
}

export type OptionsPatch = Record<string, number | string | boolean>;

export type OptionsSnapshot = {
  revision: number;
  values: Record<string, number | string | boolean>;
} | null;

export type RgbaBytes = Uint8Array | Uint8ClampedArray;

export type VisualizationSamples = Float32Array;

export interface MirrorFrameBinding extends ClassHandle {
  convert(
    _0: RgbaBytes,
    _1: number,
    _2: number,
    _3: boolean,
  ): string | undefined;
  transform(_0: RgbaBytes, _1: number, _2: number): number;
  submitAudio(_0: VisualizationSamples, _1: number): void;
  renderAudio(_0: number, _1: number, _2: number): string | undefined;
}

interface EmbindModule {
  browser_ui_audio(_0: number): number;
  browser_ui_fps(): number;
  browser_ui_volume(): number;
  SessionUiBinding: {
    new (): SessionUiBinding;
    new (_0: boolean): SessionUiBinding;
  };
  sessionRenderCleanup(): void;
  mirror_cleanup(): void;
  TerminalRendererBinding: {
    new (
      _0: number,
      _1: number,
      _2: number,
      _3: boolean,
      _4: number,
    ): TerminalRendererBinding;
  };
  faceReset(): void;
  faceDetect(_0: FacePixels, _1: number, _2: number, _3: number): number;
  faceTick(_0: number, _1: boolean): FaceValues;
  TestPatternBinding: {
    new (_0: number, _1: number): TestPatternBinding;
  };
  AudioProcessorBinding: {
    new (_0: number, _1: boolean, _2: number): AudioProcessorBinding;
    new (
      _0: number,
      _1: boolean,
      _2: number,
      _3: number,
    ): AudioProcessorBinding;
  };
  OpusCodecBinding: {
    new (_0: number, _1: number, _2: number, _3: number): OpusCodecBinding;
  };
  configureCrypto(_0: PeerBytes, _1: PeerBytes, _2: PeerBytes): number;
  PeerCryptoBinding: {
    new (
      _0: boolean,
      _1: number,
      _2: PeerBytes,
      _3: PeerBytes,
      _4: PeerBytes,
    ): PeerCryptoBinding;
  };
  ClientSessionBinding: {
    new (_0: OptionsPatch): ClientSessionBinding;
  };
  client_cleanup(): void;
  client_generate_keypair(): number;
  client_get_connection_state(): number;
  client_resolve_crypto_policy(_0: boolean, _1: number): number;
  client_handshake_begin(): number;
  client_acds_handshake_begin(_0: number): number;
  network_error_action(_0: number): number;
  setServerAddress(_0: EmbindString, _1: number): number;
  clientQuicInit(): number;
  clientQuicTick(): number;
  clientQuicEpoch(): number;
  clientQuicOutput(): QuicControlResult;
  clientQuicPacket(
    _0: boolean,
    _1: number,
    _2: number,
    _3: PeerBytes,
  ): QuicPacketResult;
  browser_error_dismiss(): void;
  parseKey(_0: number, _1: boolean, _2: KeyInput): KeyParseResult;
  keyParserError(): string;
  errorSnapshot(): number;
  errorFrame(_0: number): ErrorFrameResult;
  AVStateBinding: {
    new (): AVStateBinding;
  };
  receiveSettings(_0: ClientBytes): ClientSettingsResult;
  handleHandshake(_0: number, _1: ClientBytes): number;
  receiveHandshake(_0: boolean, _1: number, _2: ClientBytes): number;
  cryptPacket(_0: boolean, _1: ClientBytes): ClientPacketResult;
  serializePacket(_0: number, _1: ClientBytes, _2: number): ClientPacketResult;
  validateFrame(
    _0: ClientBytes,
    _1: number,
    _2: number,
    _3: number,
    _4: number,
    _5: ClientBytes,
    _6: number,
  ): ClientFrameResult;
  DiscoveryBinding: {
    new (): DiscoveryBinding;
  };
  handoffEncode(
    _0: number,
    _1: ProtocolBytes,
    _2: ProtocolBytes,
    _3: number,
    _4: number,
    _5: number,
    _6: number,
    _7: number,
  ): OptionalHandoffBytes;
  handoffDecode(_0: ProtocolBytes): HandoffActionValue;
  browser_host_create(): number;
  browser_host_destroy(): void;
  browser_host_add_client(): number;
  browser_host_set_local_client(_0: number): number;
  browser_host_remove_client(_0: number): void;
  browser_host_render(): number;
  browser_host_render_audio(): number;
  HostPacketBinding: {
    new (): HostPacketBinding;
  };
  browser_options_revision(): number;
  clientSettings(): string | undefined;
  clientPublicKey(): string | undefined;
  parsePacketJson(_0: ClientBytes): string | undefined;
  initMirror(_0: OptionsPatch): number;
  initClient(_0: OptionsPatch): number;
  patchOptions(_0: OptionsPatch, _1: number): number;
  readOptions(): OptionsSnapshot;
  optionHelp(_0: number, _1: EmbindString): string | undefined;
  sessionRenderResetStabilizer(): void;
  browser_capture_transform_required(): number;
  MirrorFrameBinding: {
    new (): MirrorFrameBinding;
  };
}

export type MainModule = WasmModule & typeof RuntimeExports & EmbindModule;
export default function MainModuleFactory(
  options?: unknown,
): Promise<MainModule>;
