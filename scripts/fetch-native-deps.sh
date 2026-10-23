#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
deps_root="${ZOOT_NATIVE_DEPS_DIR:-$repo_root/build/native-deps}"
mkdir -p "$deps_root"
fetch() {
    local name="$1" url="$2" revision="$3"
    if [[ ! -d "$deps_root/$name/.git" ]]; then
        git clone --no-checkout "$url" "$deps_root/$name"
    fi
    if ! git -C "$deps_root/$name" cat-file -e "$revision^{commit}" 2>/dev/null; then
        git -C "$deps_root/$name" fetch --depth 1 origin "$revision"
    fi
    git -C "$deps_root/$name" checkout --force --detach "$revision" >/dev/null
}
fetch BNM https://github.com/ByNameModding/BNM-Android.git 502928771983d29e37f28c78f28823dfb775a3aa
fetch Dobby https://github.com/jmpews/Dobby.git 5dfc8546954ce3b3198132ab13fddb89ee92cdd7
python3 "$repo_root/scripts/configure-bnm.py" "$deps_root/BNM"
python3 "$repo_root/scripts/configure-dobby.py" "$deps_root/Dobby"
