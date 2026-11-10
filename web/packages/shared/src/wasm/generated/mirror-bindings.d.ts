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
  dimensions(): any;
  render(_0: EmbindString, _1: number): any;
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
  processFrame(_0: RgbaBytes, _1: number, _2: number, _3: boolean): any;
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
