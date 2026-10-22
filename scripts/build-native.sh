#!/usr/bin/env bash
# 编译原生引擎（arm64-v8a + x86_64），不依赖任何第三方 hook 库
set -euo pipefail
cd "$(dirname "$0")/.."

: "${ANDROID_NDK_ROOT:=$HOME/Android/Sdk/ndk/27.1.12297006}"
TOOLCHAIN="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"

build() {  # $1=abi  $2=target-triple
    local abi="$1" triple="$2"
    local cxx="$TOOLCHAIN/${triple}-clang++"
    [ -x "$cxx" ] || { echo "找不到 $cxx，请设置 ANDROID_NDK_ROOT" >&2; return 1; }
    local out="android/app/src/main/jniLibs/$abi"
    mkdir -p "$out"
    local sources=()
    if [[ "$abi" == x86_64 ]]; then
        sources=(android/app/src/main/cpp/waydroid_loader.cpp android/app/src/main/cpp/translated_engine.cpp android/app/src/main/cpp/loaded_export.cpp)
    else
        sources=(android/app/src/main/cpp/ak_core.cpp android/app/src/main/cpp/hook.cpp)
    fi
    "$cxx" -shared -O2 -fPIC -fvisibility=hidden -std=c++17 -Wall -Wextra \
        "${sources[@]}" \
        -static-libstdc++ -llog -ldl \
        -o "$out/libakl_core.so"
    echo "已生成: $out/libakl_core.so"
    "$TOOLCHAIN/llvm-readelf" -h "$out/libakl_core.so" | grep -E "Machine|Class"
}

build arm64-v8a aarch64-linux-android26
build x86_64    x86_64-linux-android26

# Asset carrier keeps PackageManager's Waydroid ABI x86_64 while shipping the
# arm64 execution side separately. This generated file is never committed.
mkdir -p android/app/src/main/assets/akl
cp android/app/src/main/jniLibs/arm64-v8a/libakl_core.so \
   android/app/src/main/assets/akl/libakl_guest.so
