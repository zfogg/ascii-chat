// TypeScript bindings for emscripten-generated code.  Automatically generated at compile time.
declare namespace RuntimeExports {
  let HEAPU8: any;
  namespace FS {
    export let root: any;
    export let mounts: any[];
    export let devices: {};
    export let streams: any[];
    export let nextInode: number;
    export let nameTable: any;
    export let currentPath: string;
    export let initialized: boolean;
    export let ignorePermissions: boolean;
    export let filesystems: any;
    export let syncFSRequests: number;
    export let readFiles: {};
    export { ErrnoError };
    export { FSStream };
    export { FSNode };
    export function lookupPath(
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
    export function getPath(node: any): any;
    export function hashName(parentid: any, name: any): number;
    export function hashAddNode(node: any): void;
    export function hashRemoveNode(node: any): void;
    export function lookupNode(parent: any, name: any): any;
    export function createNode(
      parent: any,
      name: any,
      mode: any,
      rdev: any,
    ): any;
    export function destroyNode(node: any): void;
    export function isRoot(node: any): boolean;
    export function isMountpoint(node: any): boolean;
    export function isFile(mode: any): boolean;
    export function isDir(mode: any): boolean;
    export function isLink(mode: any): boolean;
    export function isChrdev(mode: any): boolean;
    export function isBlkdev(mode: any): boolean;
    export function isFIFO(mode: any): boolean;
    export function isSocket(mode: any): boolean;
    export function flagsToPermissionString(flag: any): string;
    export function nodePermissions(node: any, perms: any): 0 | 2;
    export function mayLookup(dir: any): any;
    export function mayCreate(dir: any, name: any): any;
    export function mayDelete(dir: any, name: any, isdir: any): any;
    export function mayOpen(node: any, flags: any): any;
    export function checkOpExists(op: any, err: any): any;
    export let MAX_OPEN_FDS: number;
    export function nextfd(): number;
    export function getStreamChecked(fd: any): any;
    export function getStream(fd: any): any;
    export function createStream(stream: any, fd?: number): any;
    export function closeStream(fd: any): void;
    export function dupStream(origStream: any, fd?: number): any;
    export function doSetAttr(stream: any, node: any, attr: any): void;
    export namespace chrdev_stream_ops {
      function open(stream: any): void;
      function llseek(): never;
    }
    export function major(dev: any): number;
    export function minor(dev: any): number;
    export function makedev(ma: any, mi: any): number;
    export function registerDevice(dev: any, ops: any): void;
    export function getDevice(dev: any): any;
    export function getMounts(mount: any): any[];
    export function syncfs(populate: any, callback: any): void;
    export function mount(type: any, opts: any, mountpoint: any): any;
    export function unmount(mountpoint: any): void;
    export function lookup(parent: any, name: any): any;
    export function mknod(path: any, mode: any, dev: any): any;
    export function statfs(path: any): any;
    export function statfsStream(stream: any): any;
    export function statfsNode(node: any): {
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
    export function create(path: any, mode?: number): any;
    export function mkdir(path: any, mode?: number): any;
    export function mkdirTree(path: any, mode: any): void;
    export function mkdev(path: any, mode: any, dev: any): any;
    export function symlink(oldpath: any, newpath: any): any;
    export function rename(old_path: any, new_path: any): void;
    export function rmdir(path: any): void;
    export function readdir(path: any): any;
    export function unlink(path: any): void;
    export function readlink(path: any): any;
    export function stat(path: any, dontFollow: any): any;
    export function fstat(fd: any): any;
    export function lstat(path: any): any;
    export function doChmod(
      stream: any,
      node: any,
      mode: any,
      dontFollow: any,
    ): void;
    export function chmod(path: any, mode: any, dontFollow: any): void;
    export function lchmod(path: any, mode: any): void;
    export function fchmod(fd: any, mode: any): void;
    export function doChown(stream: any, node: any, dontFollow: any): void;
    export function chown(path: any, uid: any, gid: any, dontFollow: any): void;
    export function lchown(path: any, uid: any, gid: any): void;
    export function fchown(fd: any, uid: any, gid: any): void;
    export function doTruncate(stream: any, node: any, len: any): void;
    export function truncate(path: any, len: any): void;
    export function ftruncate(fd: any, len: any): void;
    export function utime(path: any, atime: any, mtime: any): void;
    export function open(path: any, flags: any, mode?: number): any;
    export function close(stream: any): void;
    export function isClosed(stream: any): boolean;
    export function llseek(stream: any, offset: any, whence: any): any;
    export function read(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      position: any,
    ): any;
    export function write(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      position: any,
      canOwn: any,
    ): any;
    export function mmap(
      stream: any,
      length: any,
      position: any,
      prot: any,
      flags: any,
    ): any;
    export function msync(
      stream: any,
      buffer: any,
      offset: any,
      length: any,
      mmapFlags: any,
    ): any;
    export function ioctl(stream: any, cmd: any, arg: any): any;
    export function readFile(path: any, opts?: {}): Uint8Array<any>;
    export function writeFile(path: any, data: any, opts?: {}): void;
    export function cwd(): any;
    export function chdir(path: any): void;
    export function createDefaultDirectories(): void;
    export function createDefaultDevices(): void;
    export function createSpecialDirectories(): void;
    export function createStandardStreams(
      input: any,
      output: any,
      error: any,
    ): void;
    export function staticInit(): void;
    export function init(input: any, output: any, error: any): void;
    export function quit(): void;
    export function findObject(path: any, dontResolveLastLink: any): any;
    export function analyzePath(
      path: any,
      dontResolveLastLink: any,
    ): {
      isRoot: boolean;
      exists: boolean;
      error: number;
      name: any;
      path: any;
      object: any;
      parentExists: boolean;
      parentPath: any;
      parentObject: any;
    };
    export function createPath(
      parent: any,
      path: any,
      canRead: any,
      canWrite: any,
    ): any;
    export function createFile(
      parent: any,
      name: any,
      properties: any,
      canRead: any,
      canWrite: any,
    ): any;
    export function createDataFile(
      parent: any,
      name: any,
      data: any,
      canRead: any,
      canWrite: any,
      canOwn: any,
    ): void;
    export function createDevice(
      parent: any,
      name: any,
      input: any,
      output: any,
    ): any;
    export function forceLoadFile(obj: any): boolean;
    export function createLazyFile(
      parent: any,
      name: any,
      url: any,
      canRead: any,
      canWrite: any,
    ): any;
    export function absolutePath(): void;
    export function createFolder(): void;
    export function createLink(): void;
    export function joinPath(): void;
    export function mmapAlloc(): void;
    export function standardizePath(): void;
  }
}
declare class ErrnoError extends Error {
  constructor(errno: any);
  errno: any;
  code: string;
}
declare class FSStream {
  shared: {};
  set object(val: any);
  get object(): any;
  node: any;
  get isRead(): boolean;
  get isWrite(): boolean;
  get isAppend(): number;
  set flags(val: any);
  get flags(): any;
  set position(val: any);
  get position(): any;
}
declare class FSNode {
  constructor(parent: any, name: any, mode: any, rdev: any);
  node_ops: {};
  stream_ops: {};
  readMode: number;
  writeMode: number;
  mounted: any;
  parent: any;
  mount: any;
  id: number;
  name: any;
  mode: any;
  rdev: any;
  atime: number;
  mtime: number;
  ctime: number;
  set read(val: boolean);
  get read(): boolean;
  set write(val: boolean);
  get write(): boolean;
  get isFolder(): any;
  get isDevice(): any;
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
type RgbaBytes = Uint8Array | Uint8ClampedArray;

type VisualizationSamples = Float32Array;

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

export interface SessionUiBinding extends ClassHandle {
  valid(): boolean;
  resize(_0: number, _1: number): void;
  loading(_0: boolean, _1: EmbindString): void;
  log(_0: EmbindString): void;
  media(_0: EmbindString): number;
  key(_0: number): number;
  tick(_0: number): string | undefined;
  clear(): void;
  redraw(): void;
}

type RasterPixels = Uint8ClampedArray;

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

type FacePixels = Uint8ClampedArray;

type FaceValues = Float32Array;

type PatternPixels = Uint8ClampedArray;

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

interface EmbindModule {
  browser_options_revision(): number;
  browser_ui_volume(): number;
  browser_ui_fps(): number;
  browser_ui_audio(_0: number): number;
  browser_capture_transform_required(): number;
  mirror_cleanup(): void;
  sessionRenderResetStabilizer(): void;
  sessionRenderCleanup(): void;
  set_width(_0: number): number;
  get_width(): number;
  set_height(_0: number): number;
  get_height(): number;
  set_color_mode(_0: number): number;
  get_color_mode(): number;
  set_color_filter(_0: number): number;
  get_color_filter(): number;
  set_matrix_rain(_0: number): number;
  get_matrix_rain(): number;
  set_waveform(_0: number): number;
  get_waveform(): number;
  set_fft(_0: number): number;
  get_fft(): number;
  set_flip_x(_0: number): number;
  get_flip_x(): number;
  set_flip_y(_0: number): number;
  get_flip_y(): number;
  set_render_mode(_0: number): number;
  get_render_mode(): number;
  set_target_fps(_0: number): number;
  get_target_fps(): number;
  get_palette(): number;
  initMirror(_0: EmbindString): number;
  patchOptions(_0: EmbindString, _1: number): number;
  readOptions(): string | undefined;
  setPalette(_0: EmbindString): number;
  setPaletteChars(_0: EmbindString): number;
  getPaletteChars(): string;
  optionHelp(_0: number, _1: EmbindString): string | undefined;
  MirrorFrameBinding: {
    new (): MirrorFrameBinding;
  };
  SessionUiBinding: {
    new (): SessionUiBinding;
    new (_0: boolean): SessionUiBinding;
  };
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
}

export type MainModule = WasmModule & typeof RuntimeExports & EmbindModule;
export default function MainModuleFactory(
  options?: unknown,
): Promise<MainModule>;
