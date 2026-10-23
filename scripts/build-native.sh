#!/usr/bin/env bash
# x64 host uses ART NativeBridge; arm64 guest uses pinned BNM + Dobby.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"
sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
: "${ANDROID_NDK_ROOT:=$sdk_root/ndk/27.1.12297006}"
export ZOOT_NATIVE_DEPS_DIR="${ZOOT_NATIVE_DEPS_DIR:-$repo_root/build/native-deps}"
bash scripts/fetch-native-deps.sh
toolchain="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin"
cmake_bin="${ZOOT_CMAKE:-$sdk_root/cmake/3.22.1/bin/cmake}"
if [[ ! -x "$cmake_bin" ]]; then cmake_bin="$(command -v cmake || true)"; fi
if [[ ! -x "$cmake_bin" || ! -x "$toolchain/aarch64-linux-android26-clang++" ]]; then
    echo "需要 Android NDK 27.1.12297006 和 CMake 3.22.1；请检查 ANDROID_SDK_ROOT、ANDROID_NDK_ROOT、ZOOT_CMAKE" >&2
    exit 1
fi
"$cmake_bin" -S android/app/src/main/cpp -B build/native-arm64 -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release -DZOOT_NATIVE_DEPS_DIR="$ZOOT_NATIVE_DEPS_DIR"
"$cmake_bin" --build build/native-arm64 --parallel "${ZOOT_BUILD_JOBS:-2}"
mkdir -p android/app/src/main/jniLibs/{arm64-v8a,x86_64} android/app/src/main/assets/akl
cp build/native-arm64/libakl_core.so android/app/src/main/jniLibs/arm64-v8a/libakl_core.so
"$toolchain/llvm-strip" --strip-debug android/app/src/main/jniLibs/arm64-v8a/libakl_core.so
cp android/app/src/main/jniLibs/arm64-v8a/libakl_core.so android/app/src/main/assets/akl/libakl_guest.so
"$toolchain/x86_64-linux-android26-clang++" -shared -O2 -fPIC -fvisibility=hidden \
    -std=c++17 -Wall -Wextra android/app/src/main/cpp/waydroid_loader.cpp \
    android/app/src/main/cpp/translated_engine.cpp android/app/src/main/cpp/loaded_export.cpp \
    -static-libstdc++ -llog -ldl -o android/app/src/main/jniLibs/x86_64/libakl_core.so
for abi in arm64-v8a x86_64; do
    echo "已生成 $abi/libakl_core.so"
    "$toolchain/llvm-readelf" -h "android/app/src/main/jniLibs/$abi/libakl_core.so" | grep -E 'Machine|Class'
done
