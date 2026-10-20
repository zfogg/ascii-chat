"""Exercise dependency discovery with stale hints and Windows tools under spaces."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def configure(source, build, env=None):
    subprocess.run(["cmake", "-G", "Ninja", "-S", str(source), "-B", str(build)], check=True, env=env)


def opus(root, fixture):
    source = fixture / "opus"
    headers = source / "current prefix/include/opus"
    headers.mkdir(parents=True)
    (headers / "opus.h").write_text("/* Fixture Opus header. */\n", encoding="utf-8")
    (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(opus_paths NONE)
include("{root.as_posix()}/cmake/utils/FindDependency.cmake")
# A restored configure cache can contain pkg-config hints from an old prefix.
macro(find_dependency_library)
    set(OPUS_FOUND TRUE)
    set(OPUS_INCLUDE_DIRS "${{CMAKE_CURRENT_SOURCE_DIR}}/removed prefix/include")
endmacro()
set(CMAKE_PREFIX_PATH "${{CMAKE_CURRENT_SOURCE_DIR}}/current prefix")
include("{root.as_posix()}/cmake/deps/Opus.cmake")
if(NOT EXISTS "${{OPUS_INCLUDE_DIRS}}/opus.h")
    message(FATAL_ERROR "Required Opus header was not resolved")
endif()
''', encoding="utf-8")
    configure(source, source / "build")
    configure(source, source / "build")
    print("PASS: Opus resolves required headers despite stale dependency hints", flush=True)


def bearssl(root, fixture, args):
    source = fixture / "bearssl source"
    vendor = source / "deps/ascii-chat-deps/bearssl"
    (vendor / "mk").mkdir(parents=True)
    (vendor / "inc").mkdir()
    (vendor / "src").mkdir()
    (vendor / "stub.c").write_text("int bearssl_path_probe(void) { return 507; }\n", encoding="utf-8")
    shutil.copy2(root / "deps/ascii-chat-deps/bearssl/src/inner.h", vendor / "src/inner.h")
    for relative in ["cmake/utils/Patching.cmake", "cmake/deps/patches/bearssl-0-windows-clang.patch"]:
        target = source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, target)
    # Wrappers retain a space in the executable path even on installations
    # whose real LLVM directory has none.
    tools = fixture / "compiler tools"
    tools.mkdir()
    for name, target in [("clang-cl", args.clang_cl), ("llvm-lib", args.llvm_lib)]:
        (tools / (name + ".cmd")).write_text(f'@"{Path(target).resolve()}" %*\n', encoding="utf-8")
    (vendor / "mk/NMake.mk").write_text('''lib: build/bearssls.lib
build/bearssls.lib: build/obj/stub.obj
\t$(AR) -nologo -out:build/bearssls.lib build/obj/stub.obj
build/obj/stub.obj: stub.c
\t$(CC) -nologo -c -Fobuild/obj/stub.obj stub.c
''', encoding="utf-8")
    (source / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.20)
project(bearssl_paths NONE)
set(ASCIICHAT_DEPS_CACHE_ROOT "${{CMAKE_BINARY_DIR}}/cache")
set(ASCIICHAT_DEPS_CACHE_DIR "${{CMAKE_BINARY_DIR}}/cache/Release")
file(MAKE_DIRECTORY "${{ASCIICHAT_DEPS_CACHE_ROOT}}")
set(ASCIICHAT_NMAKE_EXECUTABLE "{Path(args.nmake).resolve().as_posix()}")
set(ASCIICHAT_CLANG_CL_EXECUTABLE "{tools.as_posix()}/clang-cl.cmd")
set(ASCIICHAT_LLVM_LIB_EXECUTABLE "{tools.as_posix()}/llvm-lib.cmd")
include("{root.as_posix()}/cmake/deps/BearSSL.cmake")
if(NOT EXISTS "${{BEARSSL_LIB}}")
    message(FATAL_ERROR "BearSSL archive was not built")
endif()
''', encoding="utf-8")
    env = dict(os.environ, MAKEFLAGS="--invalid-inherited-makeflags", NMAKEFLAGS="/invalid-inherited-nmakeflags")
    configure(source, source / "build", env)
    configure(source, source / "build", env)
    print("PASS: BearSSL builds and reuses its archive with spaced tool paths and inherited make flags", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nmake")
    parser.add_argument("--clang-cl")
    parser.add_argument("--llvm-lib")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="ascii dependency paths ") as directory:
        fixture = Path(directory)
        opus(root, fixture)
        if os.name == "nt":
            assert args.nmake and args.clang_cl and args.llvm_lib, "Windows probes require configured build tools"
            bearssl(root, fixture, args)


if __name__ == "__main__":
    main()
