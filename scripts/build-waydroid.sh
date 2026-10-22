#!/usr/bin/env bash
# Build in a disposable copy so trimming ABIs never changes the source checkout.
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
python3 - "$repo_root" "$build_dir/project" <<'PY'
import shutil
import sys
shutil.copytree(sys.argv[1], sys.argv[2], ignore=shutil.ignore_patterns(
    '.git', 'build', '.dart_tool', '.gradle', '.kotlin', '.idea', '*.apk', '*.iml'))
PY
cd "$build_dir/project"
bash scripts/build-native.sh
if [[ ! -f android/app/libs/api-82.jar ]]; then
    bash scripts/fetch-xposed-api.sh
fi
rm -rf android/app/src/main/jniLibs/arm64-v8a \
       android/app/src/main/jniLibs/armeabi-v7a android/app/src/main/jniLibs/x86
flutter pub get
flutter build apk --release --target-platform android-x64 -Pabi=x86_64
python3 scripts/verify-apk.py build/app/outputs/flutter-apk/app-release.apk \
    --abis x86_64 --engine-abis x86_64 --guest
mkdir -p "$repo_root/build"
cp build/app/outputs/flutter-apk/app-release.apk "$repo_root/build/zoot-waydroid-x64.apk"
echo "APK: $repo_root/build/zoot-waydroid-x64.apk"
