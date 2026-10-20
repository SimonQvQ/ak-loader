#!/usr/bin/env bash
# 从源码编译 arm64 原生引擎（不依赖任何第三方 hook 库）
set -euo pipefail
cd "$(dirname "$0")/.."

: "${ANDROID_NDK_ROOT:=$HOME/Android/Sdk/ndk/27.1.12297006}"
CXX="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++"
[ -x "$CXX" ] || { echo "找不到 $CXX，请设置 ANDROID_NDK_ROOT" >&2; exit 1; }

out=android/app/src/main/jniLibs/arm64-v8a
mkdir -p "$out"
"$CXX" -shared -O2 -fPIC -fvisibility=hidden -std=c++17 -Wall \
    android/app/src/main/cpp/ak_core.cpp \
    android/app/src/main/cpp/hook.cpp \
    -llog -ldl \
    -o "$out/libakl_core.so"
echo "已生成: $out/libakl_core.so"
"$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm" -D --defined-only "$out/libakl_core.so"
