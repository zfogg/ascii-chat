#!/bin/bash
set -e
rm -rf /tmp/ascii-chat-test
mkdir -p /tmp/ascii-chat-test

export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++

cmake -B /tmp/ascii-chat-test -S . -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASCIICHAT_LLVM_CONFIG_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-config \
  -DASCIICHAT_CLANG_EXECUTABLE=/opt/homebrew/opt/llvm/bin/clang \
  -DASCIICHAT_CLANG_PLUS_PLUS_EXECUTABLE=/opt/homebrew/opt/llvm/bin/clang++ \
  -DASCIICHAT_LLVM_AR_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-ar \
  -DASCIICHAT_LLVM_RANLIB_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-ranlib \
  -DASCIICHAT_LLVM_NM_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-nm \
  -DASCIICHAT_LLVM_READELF_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-readelf \
  -DASCIICHAT_LLVM_OBJDUMP_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-objdump \
  -DASCIICHAT_LLVM_STRIP_EXECUTABLE=/opt/homebrew/opt/llvm/bin/llvm-strip \
  -DASCIICHAT_LLD_EXECUTABLE=/opt/homebrew/opt/lld/bin/ld.lld \
  -DASCIICHAT_DEPS_CACHE_DIR=/tmp/ascii-chat-test/.deps-cache

cmake --build /tmp/ascii-chat-test --target ascii-chat
