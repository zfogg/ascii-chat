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
      new (errno: unknown): {
        cause?: unknown;
        name: string;
        errno: unknown;
        code: string | undefined;
        message: string;
        stack?: string;
      };
    };
    FSStream: {
      new (): {
        shared: {};
        get object(): unknown;
        set object(val: unknown);
        node: unknown;
        get isRead(): boolean;
        get isWrite(): boolean;
        get isAppend(): number;
        flags: unknown;
        position: unknown;
      };
    };
    FSNode: {
      new (
        parent: unknown,
        name: unknown,
        mode: unknown,
        rdev: unknown,
      ): {
        node_ops: {};
        stream_ops: {};
        readMode: number;
        writeMode: number;
        mounted: null;
        parent: unknown;
        mount: unknown;
        id: number;
        name: unknown;
        mode: unknown;
        rdev: unknown;
        atime: number;
        mtime: number;
        ctime: number;
        get read(): boolean;
        set read(val: boolean);
        get write(): boolean;
        set write(val: boolean);
        readonly isFolder: unknown;
        readonly isDevice: unknown;
        addListener(
          cb: unknown,
          exclusive?: boolean,
        ): {
          listeners: unknown;
          entry: {
            cb: unknown;
            exclusive: boolean;
          };
        };
        notifyListeners(flags: unknown): void;
        exclTurn: unknown;
      };
    };
    lookupPath(
      path: unknown,
      opts?: {},
    ):
      | {
          path: string;
          node?: undefined;
        }
      | {
          path: string;
          node: unknown;
        };
    getPath(node: unknown): unknown;
    hashName(parentid: unknown, name: unknown): number;
    hashAddNode(node: unknown): void;
    hashRemoveNode(node: unknown): void;
    lookupNode(parent: unknown, name: unknown): unknown;
    createNode(
      parent: unknown,
      name: unknown,
      mode: unknown,
      rdev?: undefined,
    ): unknown;
    destroyNode(node: unknown): void;
    isRoot(node: unknown): boolean;
    isMountpoint(node: unknown): boolean;
    isFile(mode: unknown): boolean;
    isDir(mode: unknown): boolean;
    isLink(mode: unknown): boolean;
    isChrdev(mode: unknown): boolean;
    isBlkdev(mode: unknown): boolean;
    isFIFO(mode: unknown): boolean;
    isSocket(mode: unknown): boolean;
    flagsToPermissionString(flag: unknown): string;
    nodePermissions(node: unknown, perms: unknown): 0 | 2;
    mayLookup(dir: unknown): unknown;
    mayCreate(dir: unknown, name: unknown): unknown;
    mayDelete(dir: unknown, name: unknown, isdir: unknown): unknown;
    mayOpen(node: unknown, flags: unknown): unknown;
    checkOpExists(op: unknown, err: unknown): unknown;
    MAX_OPEN_FDS: number;
    nextfd(): number;
    getStreamChecked(fd: unknown): unknown;
    getStream: (fd: unknown) => unknown;
    createStream(stream: unknown, fd?: number): unknown;
    closeStream(fd: unknown): void;
    dupStream(origStream: unknown, fd?: number): unknown;
    doSetAttr(stream: unknown, node: unknown, attr: unknown): void;
    chrdev_stream_ops: {
      open(stream: unknown): void;
      llseek(): never;
    };
    major: (dev: unknown) => number;
    minor: (dev: unknown) => number;
    makedev: (ma: unknown, mi: unknown) => number;
    registerDevice(dev: unknown, ops: unknown): void;
    getDevice: (dev: unknown) => unknown;
    getMounts(mount: unknown): unknown[];
    syncfs(populate: unknown, callback: unknown): void;
    mount(type: unknown, opts: unknown, mountpoint: unknown): unknown;
    unmount(mountpoint: unknown): void;
    lookup(parent: unknown, name: unknown): unknown;
    mknod(path: unknown, mode: unknown, dev: unknown): unknown;
    statfs(path: unknown): unknown;
    statfsStream(stream: unknown): unknown;
    statfsNode(node: unknown): {
      bsize: number;
      frsize: number;
      blocks: number;
      bfree: number;
      bavail: number;
      files: unknown;
      ffree: number;
      fsid: number;
      flags: number;
      namelen: number;
    };
    create(path: unknown, mode?: number): unknown;
    mkdir(path: unknown, mode?: number): unknown;
    mkdirTree(path: unknown, mode?: number): void;
    mkdev(path: unknown, mode: unknown, dev?: undefined): unknown;
    symlink(oldpath: unknown, newpath: unknown): unknown;
    link(oldpath: unknown, newpath: unknown, flags: unknown): unknown;
    rename(old_path: unknown, new_path: unknown): void;
    rmdir(path: unknown): void;
    readdir(path: unknown): unknown;
    unlink(path: unknown): void;
    readlink(path: unknown): unknown;
    stat(path: unknown, dontFollow?: boolean): unknown;
    fstat(fd: unknown): unknown;
    lstat(path: unknown): unknown;
    doChmod(
      stream: unknown,
      node: unknown,
      mode: unknown,
      dontFollow?: boolean,
    ): void;
    chmod(path: unknown, mode: unknown, dontFollow?: boolean): void;
    lchmod(path: unknown, mode: unknown): void;
    fchmod(fd: unknown, mode: unknown): void;
    doChown(stream: unknown, node: unknown, dontFollow?: boolean): void;
    chown(
      path: unknown,
      uid: unknown,
      gid: unknown,
      dontFollow?: boolean,
    ): void;
    lchown(path: unknown, uid: unknown, gid: unknown): void;
    fchown(fd: unknown, uid: unknown, gid: unknown): void;
    doTruncate(stream: unknown, node: unknown, len: unknown): void;
    truncate(path: unknown, len: unknown): void;
    ftruncate(fd: unknown, len: unknown): void;
    utime(
      path: unknown,
      atime: unknown,
      mtime: unknown,
      dontFollow?: boolean,
    ): void;
    open(path: unknown, flags: unknown, mode?: number): unknown;
    close(stream: unknown): void;
    isClosed(stream: unknown): boolean;
    llseek(stream: unknown, offset: unknown, whence: unknown): unknown;
    read(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      position?: undefined,
    ): unknown;
    write(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      position?: undefined,
      canOwn?: undefined,
    ): unknown;
    mmap(
      stream: unknown,
      length: unknown,
      position: unknown,
      prot: unknown,
      flags: unknown,
    ): unknown;
    msync(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      mmapFlags: unknown,
    ): unknown;
    ioctl(stream: unknown, cmd: unknown, arg: unknown): unknown;
    readFile(path: unknown, opts?: {}): Uint8Array;
    writeFile(path: unknown, data: unknown, opts?: {}): void;
    cwd: () => unknown;
    chdir(path: unknown): void;
    createDefaultDirectories(): void;
    createDefaultDevices(): void;
    createSpecialDirectories(): void;
    createStandardStreams(
      input: unknown,
      output: unknown,
      error: unknown,
    ): void;
    staticInit(): void;
    init(input?: undefined, output?: undefined, error?: undefined): void;
    quit(): void;
    analyzePath(
      path: unknown,
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
      parent: unknown,
      path: unknown,
      canRead?: undefined,
      canWrite?: undefined,
    ): unknown;
    createFile(
      parent: unknown,
      name: unknown,
      properties: unknown,
      canRead?: undefined,
      canWrite?: undefined,
    ): unknown;
    createDataFile(
      parent: unknown,
      name: unknown,
      data?: undefined,
      canRead?: undefined,
      canWrite?: undefined,
      canOwn?: undefined,
    ): void;
    createDevice(
      parent: unknown,
      name: unknown,
      input?: undefined,
      output?: undefined,
    ): unknown;
    forceLoadFile(obj: unknown): true | undefined;
    createLazyFile(
      parent: unknown,
      name: unknown,
      url: unknown,
      canRead?: undefined,
      canWrite?: undefined,
    ): unknown;
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
  error(): number;
  close(): void;
  resize(_0: number, _1: number): void;
  loading(_0: boolean, _1: EmbindString): void;
  log(_0: EmbindString): void;
  media(_0: EmbindString): number;
  key(_0: number): number;
  clear(): void;
  redraw(): void;
  tick(_0: number): string | undefined;
}

export interface MirrorSessionBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  resetRender(): void;
  close(): void;
}

export interface AsciiRendererBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  dimensions(): RendererDimensions;
  render(_0: EmbindString, _1: number): Uint8ClampedArray;
}

export type FacePixels = Uint8ClampedArray;

export type FaceValues = Float32Array;

export interface FaceFollowBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  reset(): void;
  detect(_0: FacePixels, _1: number, _2: number, _3: number): number;
  tick(_0: number, _1: boolean): FaceValues;
}

export type PatternPixels = Uint8ClampedArray;

export interface TestPatternBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
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
  error(): number;
  close(): void;
  process(_0: AudioSamples, _1: AudioSamples): number;
  processBatch(
    _0: ReadonlyArray<AudioSamples>,
    _1: ReadonlyArray<AudioSamples>,
  ): number;
}

export interface OpusCodecBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  encode(_0: PcmSamples): NullableOpusBytes;
  decode(_0: OpusBytes, _1: number): NullablePcmSamples;
}

export interface AudioSessionBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  process(_0: AudioSamples, _1: AudioSamples): AudioSessionResult;
}

export type PeerBytes = Uint8Array;

export type NullablePeerBytes = Uint8Array | null;

export interface BrowserSessionBinding extends ClassHandle {
  valid(): boolean;
  ready(): boolean;
  error(): string;
  close(): void;
  process(_0: SessionEvent): SessionResult;
  processBatch(_0: ReadonlyArray<SessionEvent>): ReadonlyArray<SessionResult>;
}

export type PacketTypes = Readonly<Record<string, number>>;
export type ClientBytes = Uint8Array;
export type WasmRecord = Record<string, unknown>;
export interface RendererDimensions {
  cols: number;
  rows: number;
  width: number;
  height: number;
}
export interface MirrorFrameResult extends WasmRecord {
  ok: boolean;
  code: number;
  message?: string;
  retryable?: boolean;
  text: string | null;
  width: number;
  height: number;
  changed: boolean;
}
export interface AudioSessionResult {
  code?: number;
  samples?: Float32Array;
  payload?: Uint8Array;
}
export type SessionEvent = WasmRecord;
export interface SessionResult extends WasmRecord {
  code?: number;
  json?: string | null;
  packetTypes?: Readonly<Record<string, number>>;
  bytes?: Uint8Array;
  decodeFailed?: boolean;
}
export interface ProtocolInfo extends WasmRecord {
  packetTypes: Readonly<Record<string, number>>;
}
export interface PacketOutput {
  type: number;
  payload: Uint8Array;
}
export type PacketOutputList = ReadonlyArray<PacketOutput>;
export interface HostPacket {
  id: number;
  bytes: Uint8Array;
}

export interface ClientSessionBinding extends ClassHandle {
  valid(): boolean;
  packetTypes(): PacketTypes;
  protocolInfo(): ProtocolInfo;
  error(): number;
  close(): void;
  process(_0: SessionEvent): SessionResult;
  processBatch(_0: ReadonlyArray<SessionEvent>): ReadonlyArray<SessionResult>;
}

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

export interface KeyParserBinding extends ClassHandle {}

export interface ErrorBinding extends ClassHandle {}

export interface AVStateBinding extends ClassHandle {
  valid(): boolean;
  close(): void;
  generation(): number;
  begin(): number;
  complete(_0: number): boolean;
  current(_0: number): boolean;
  active(): boolean;
  beginStop(): void;
  finishStop(): void;
}

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

export type ProtocolBytes = Uint8Array;

export interface DiscoveryBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  process(_0: SessionEvent): SessionResult;
  processBatch(_0: ReadonlyArray<SessionEvent>): ReadonlyArray<SessionResult>;
  drainOutputs(): PacketOutputList;
}

export interface HandoffBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  process(_0: SessionEvent): SessionResult;
}

export interface BrowserHostBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  addClient(): number;
  removeClient(_0: number): void;
  setLocalClient(_0: number): number;
  render(): number;
  renderAudio(): number;
  drainPackets(): ReadonlyArray<HostPacket>;
  receive(_0: number, _1: number, _2: ProtocolBytes): number;
}

export type OptionsPatch = Record<string, number | string | boolean>;

export type OptionsSnapshot = {
  revision: number;
  values: Record<string, number | string | boolean>;
} | null;

export interface OptionsBinding extends ClassHandle {}

export type RgbaBytes = Uint8Array | Uint8ClampedArray;

export type VisualizationSamples = Float32Array;

export interface MirrorFrameBinding extends ClassHandle {
  valid(): boolean;
  error(): number;
  close(): void;
  reset(): void;
  convert(
    _0: RgbaBytes,
    _1: number,
    _2: number,
    _3: boolean,
  ): string | undefined;
  processFrame(
    _0: RgbaBytes,
    _1: number,
    _2: number,
    _3: boolean,
  ): MirrorFrameResult;
  transform(_0: RgbaBytes, _1: number, _2: number): number;
  submitAudio(_0: VisualizationSamples, _1: number): void;
  renderAudio(_0: number, _1: number, _2: number): string | undefined;
}

interface EmbindModule {
  SessionUiBinding: {
    new (): SessionUiBinding;
    new (_0: boolean): SessionUiBinding;
    setAudio(_0: number): number;
    fps(): number;
    volume(): number;
  };
  MirrorSessionBinding: {
    new (_0: OptionsPatch): MirrorSessionBinding;
  };
  AsciiRendererBinding: {
    new (
      _0: number,
      _1: number,
      _2: number,
      _3: boolean,
      _4: number,
    ): AsciiRendererBinding;
  };
  FaceFollowBinding: {
    new (): FaceFollowBinding;
  };
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
  AudioSessionBinding: {
    new (
      _0: number,
      _1: boolean,
      _2: number,
      _3: number,
      _4: number,
    ): AudioSessionBinding;
  };
  BrowserSessionBinding: {
    new (
      _0: boolean,
      _1: number,
      _2: PeerBytes,
      _3: PeerBytes,
      _4: PeerBytes,
    ): BrowserSessionBinding;
  };
  ClientSessionBinding: {
    new (_0: OptionsPatch): ClientSessionBinding;
    errorAction(_0: number): number;
  };
  KeyParserBinding: {
    parse(_0: number, _1: boolean, _2: KeyInput): KeyParseResult;
    error(): string;
  };
  ErrorBinding: {
    dismiss(): void;
    snapshot(): number;
    frame(_0: number): ErrorFrameResult;
  };
  AVStateBinding: {
    new (): AVStateBinding;
  };
  DiscoveryBinding: {
    new (): DiscoveryBinding;
  };
  HandoffBinding: {
    new (_0: ProtocolBytes, _1: ProtocolBytes): HandoffBinding;
  };
  BrowserHostBinding: {
    new (): BrowserHostBinding;
  };
  OptionsBinding: {
    revision(): number;
    patch(_0: OptionsPatch, _1: number): number;
    read(): OptionsSnapshot;
    help(_0: number, _1: EmbindString): string | undefined;
  };
  MirrorFrameBinding: {
    new (): MirrorFrameBinding;
    transformRequired(): number;
  };
}

export type MainModule = WasmModule & typeof RuntimeExports & EmbindModule;
export default function MainModuleFactory(
  options?: unknown,
): Promise<MainModule>;
