// TypeScript bindings for emscripten-generated code.  Automatically generated at compile time.
declare namespace RuntimeExports {
  let HEAPU8: Uint8Array;
  namespace FS {
    export let root: unknown;
    export let mounts: unknown[];
    export let devices: {};
    export let streams: unknown[];
    export let nextInode: number;
    export let nameTable: unknown[];
    export let currentPath: string;
    export let initialized: boolean;
    export let ignorePermissions: boolean;
    export let syncFSRequests: number;
    export { ErrnoError };
    export { FSStream };
    export { FSNode };
    export function lookupPath(
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
    export function getPath(node: unknown): unknown;
    export function hashName(parentid: unknown, name: unknown): number;
    export function hashAddNode(node: unknown): void;
    export function hashRemoveNode(node: unknown): void;
    export function lookupNode(parent: unknown, name: unknown): unknown;
    export function createNode(
      parent: unknown,
      name: unknown,
      mode: unknown,
      rdev?: unknown,
    ): unknown;
    export function destroyNode(node: unknown): void;
    export function isRoot(node: unknown): boolean;
    export function isMountpoint(node: unknown): boolean;
    export function isFile(mode: unknown): boolean;
    export function isDir(mode: unknown): boolean;
    export function isLink(mode: unknown): boolean;
    export function isChrdev(mode: unknown): boolean;
    export function isBlkdev(mode: unknown): boolean;
    export function isFIFO(mode: unknown): boolean;
    export function isSocket(mode: unknown): boolean;
    export function flagsToPermissionString(flag: unknown): string;
    export function nodePermissions(node: unknown, perms: unknown): 0 | 2;
    export function mayLookup(dir: unknown): unknown;
    export function mayCreate(dir: unknown, name: unknown): unknown;
    export function mayDelete(
      dir: unknown,
      name: unknown,
      isdir: unknown,
    ): unknown;
    export function mayOpen(node: unknown, flags: unknown): unknown;
    export function checkOpExists(op: unknown, err: unknown): unknown;
    export function nextfd(): number;
    export function getStreamChecked(fd: unknown): unknown;
    export function getStream(fd: unknown): unknown;
    export function createStream(stream: unknown, fd?: number): unknown;
    export function closeStream(fd: unknown): void;
    export function dupStream(origStream: unknown, fd?: number): unknown;
    export function doSetAttr(
      stream: unknown,
      node: unknown,
      attr: unknown,
    ): void;
    export namespace chrdev_stream_ops {
      function open(stream: unknown): void;
      function llseek(): never;
    }
    export function major(dev: unknown): number;
    export function minor(dev: unknown): number;
    export function makedev(ma: unknown, mi: unknown): number;
    export function registerDevice(dev: unknown, ops: unknown): void;
    export function getDevice(dev: unknown): unknown;
    export function getMounts(mount: unknown): unknown[];
    export function syncfs(populate: unknown, callback: unknown): void;
    export function mount(
      type: unknown,
      opts: unknown,
      mountpoint: unknown,
    ): unknown;
    export function unmount(mountpoint: unknown): void;
    export function lookup(parent: unknown, name: unknown): unknown;
    export function mknod(path: unknown, mode: unknown, dev: unknown): unknown;
    export function statfs(path: unknown): unknown;
    export function statfsStream(stream: unknown): unknown;
    export function statfsNode(node: unknown): {
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
    export function create(path: unknown, mode?: number): unknown;
    export function mkdir(path: unknown, mode?: number): unknown;
    export function mkdirTree(path: unknown, mode?: number): void;
    export function mkdev(path: unknown, mode: unknown, dev?: unknown): unknown;
    export function symlink(oldpath: unknown, newpath: unknown): unknown;
    export function link(
      oldpath: unknown,
      newpath: unknown,
      flags: unknown,
    ): unknown;
    export function rename(old_path: unknown, new_path: unknown): void;
    export function rmdir(path: unknown): void;
    export function readdir(path: unknown): unknown;
    export function unlink(path: unknown): void;
    export function readlink(path: unknown): unknown;
    export function stat(path: unknown, dontFollow?: boolean): unknown;
    export function fstat(fd: unknown): unknown;
    export function lstat(path: unknown): unknown;
    export function doChmod(
      stream: unknown,
      node: unknown,
      mode: unknown,
      dontFollow?: boolean,
    ): void;
    export function chmod(
      path: unknown,
      mode: unknown,
      dontFollow?: boolean,
    ): void;
    export function lchmod(path: unknown, mode: unknown): void;
    export function fchmod(fd: unknown, mode: unknown): void;
    export function doChown(
      stream: unknown,
      node: unknown,
      dontFollow?: boolean,
    ): void;
    export function chown(
      path: unknown,
      uid: unknown,
      gid: unknown,
      dontFollow?: boolean,
    ): void;
    export function lchown(path: unknown, uid: unknown, gid: unknown): void;
    export function fchown(fd: unknown, uid: unknown, gid: unknown): void;
    export function doTruncate(
      stream: unknown,
      node: unknown,
      len: unknown,
    ): void;
    export function truncate(path: unknown, len: unknown): void;
    export function ftruncate(fd: unknown, len: unknown): void;
    export function utime(
      path: unknown,
      atime: unknown,
      mtime: unknown,
      dontFollow?: boolean,
    ): void;
    export function open(path: unknown, flags: unknown, mode?: number): unknown;
    export function close(stream: unknown): void;
    export function isClosed(stream: unknown): boolean;
    export function llseek(
      stream: unknown,
      offset: unknown,
      whence: unknown,
    ): unknown;
    export function read(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      position?: unknown,
    ): unknown;
    export function write(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      position?: unknown,
      canOwn?: unknown,
    ): unknown;
    export function mmap(
      stream: unknown,
      length: unknown,
      position: unknown,
      prot: unknown,
      flags: unknown,
    ): unknown;
    export function msync(
      stream: unknown,
      buffer: unknown,
      offset: unknown,
      length: unknown,
      mmapFlags: unknown,
    ): unknown;
    export function ioctl(stream: unknown, cmd: unknown, arg: unknown): unknown;
    export function readFile(path: unknown, opts?: {}): Uint8Array;
    export function writeFile(path: unknown, data: unknown, opts?: {}): void;
    export function cwd(): unknown;
    export function chdir(path: unknown): void;
    export function createDefaultDirectories(): void;
    export function createDefaultDevices(): void;
    export function createSpecialDirectories(): void;
    export function createStandardStreams(
      input: unknown,
      output: unknown,
      error: unknown,
    ): void;
    export function staticInit(): void;
    export function init(
      input?: unknown,
      output?: unknown,
      error?: unknown,
    ): void;
    export function quit(): void;
    export function analyzePath(
      path: unknown,
      dontResolveLastLink?: boolean,
    ): {
      isRoot: boolean;
      exists: boolean;
      error: number;
      name: unknown;
      path: unknown;
      object: unknown;
      parentExists: boolean;
      parentPath: unknown;
      parentObject: unknown;
    };
    export function createPath(
      parent: unknown,
      path: unknown,
      canRead?: unknown,
      canWrite?: unknown,
    ): unknown;
    export function createFile(
      parent: unknown,
      name: unknown,
      properties: unknown,
      canRead?: unknown,
      canWrite?: unknown,
    ): unknown;
    export function createDataFile(
      parent: unknown,
      name: unknown,
      data?: unknown,
      canRead?: unknown,
      canWrite?: unknown,
      canOwn?: unknown,
    ): void;
    export function createDevice(
      parent: unknown,
      name: unknown,
      input?: unknown,
      output?: unknown,
    ): unknown;
    export function forceLoadFile(obj: unknown): boolean;
    export function createLazyFile(
      parent: unknown,
      name: unknown,
      url: unknown,
      canRead?: unknown,
      canWrite?: unknown,
    ): unknown;
  }
}
declare class ErrnoError extends Error {
  constructor(errno: unknown);
  errno: unknown;
  code: string;
}
declare class FSStream {
  shared: {};
  set object(val: unknown);
  get object(): unknown;
  node: unknown;
  get isRead(): boolean;
  get isWrite(): boolean;
  get isAppend(): number;
  set flags(val: unknown);
  get flags(): unknown;
  set position(val: unknown);
  get position(): unknown;
}
declare class FSNode {
  constructor(parent: unknown, name: unknown, mode: unknown, rdev: unknown);
  node_ops: {};
  stream_ops: {};
  readMode: number;
  writeMode: number;
  mounted: unknown;
  parent: unknown;
  mount: unknown;
  id: number;
  name: unknown;
  mode: unknown;
  rdev: unknown;
  atime: number;
  mtime: number;
  ctime: number;
  set read(val: boolean);
  get read(): boolean;
  set write(val: boolean);
  get write(): boolean;
  get isFolder(): unknown;
  get isDevice(): unknown;
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
}
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
