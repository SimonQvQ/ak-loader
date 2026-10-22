#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
java_bin="$(readlink -f "$(command -v javac)")"
jdk_root="${JAVA_HOME:-$(dirname "$(dirname "$java_bin")")}"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$jdk_root/include" -I"$jdk_root/include/linux" -Iandroid/app/src/main/cpp \
    tests/translated_engine_test.cpp android/app/src/main/cpp/translated_engine.cpp \
    -o "$test_dir/translated-engine-test"
"$test_dir/translated-engine-test" "$test_dir"
