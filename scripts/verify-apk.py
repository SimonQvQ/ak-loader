#!/usr/bin/env python3
"""Check packaged ABIs, ELF identities and the Android R resources.arsc rule."""
import argparse
import struct
import zipfile


def verify(path, abis, engine_abis, guest=False):
    machines = {"arm64-v8a": 183, "armeabi-v7a": 40, "x86_64": 62, "x86": 3}
    with zipfile.ZipFile(path) as apk:
        libs = [i for i in apk.infolist() if i.filename.startswith("lib/") and i.filename.endswith(".so")]
        actual = {i.filename.split("/")[1] for i in libs}
        if actual != abis:
            raise ValueError(f"ABI 不匹配：要求 {sorted(abis)}，实际 {sorted(actual)}")
        for abi in engine_abis:
            apk.getinfo(f"lib/{abi}/libakl_core.so")
        for info in libs:
            with apk.open(info) as lib:
                header = lib.read(20)
            abi = info.filename.split("/")[1]
            if len(header) != 20 or header[:4] != b"\x7fELF" or header[5] != 1:
                raise ValueError(f"不是受支持的小端 ELF：{info.filename}")
            machine = struct.unpack_from("<H", header, 18)[0]
            expected_class = 2 if abi in {"arm64-v8a", "x86_64"} else 1
            if machine != machines[abi] or header[4] != expected_class:
                raise ValueError(f"ELF 架构与目录不符：{info.filename}")
        if guest:
            for license_name in ("BNM.txt", "Dobby.txt"):
                apk.getinfo(f"assets/licenses/{license_name}")
            with apk.open("assets/akl/libakl_guest.so") as lib:
                header = lib.read(20)
            if (len(header) != 20 or header[:6] != b"\x7fELF\x02\x01"
                    or struct.unpack_from("<H", header, 18)[0] != 183):
                raise ValueError("Waydroid guest 必须为 arm64 ELF")
        res = apk.getinfo("resources.arsc")
        if res.compress_type != zipfile.ZIP_STORED:
            raise ValueError("resources.arsc 必须不压缩")
        with open(path, "rb") as source:
            source.seek(res.header_offset)
            local = source.read(30)
        if local[:4] != b"PK\x03\x04":
            raise ValueError("resources.arsc 本地 ZIP 头无效")
        name_len, extra_len = struct.unpack_from("<HH", local, 26)
        if (res.header_offset + 30 + name_len + extra_len) % 4:
            raise ValueError("resources.arsc 必须按 4 字节对齐")
    print(f"{path}: ABI={','.join(sorted(actual))}，ELF / resources.arsc 检查通过")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk")
    parser.add_argument("--abis", required=True, help="Exact comma-separated ABI set")
    parser.add_argument("--engine-abis", required=True, help="ABIs requiring libakl_core.so")
    parser.add_argument("--guest", action="store_true", help="Require the arm64 guest asset")
    args = parser.parse_args()
    try:
        verify(args.apk, set(args.abis.split(",")), set(args.engine_abis.split(",")), args.guest)
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"APK 校验失败：{error}\n")
