#!/usr/bin/env bash
# 下载 Xposed/LSPosed 编译期 API（只用于编译，运行时不打包进 APK）
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p android/app/libs
out=android/app/libs/api-82.jar
if [ -f "$out" ]; then
  echo "已存在: $out"
  exit 0
fi
curl -fSL -o "$out" https://api.xposed.info/de/robv/android/xposed/api/82/api-82.jar
echo "已下载: $out"
