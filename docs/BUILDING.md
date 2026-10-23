# 构建与调试

## 工具链

已验证 Linux x86_64：

| 工具 | 版本 |
| --- | --- |
| Flutter / Dart | 3.47.7 / 3.13.5 |
| JDK | 21 |
| Android platform | 36 |
| Android build-tools | 35.0.0 |
| Android NDK | 27.1.12297006 |
| CMake | 3.22.1 |

另需 Git、Python 3、curl、Ninja 和支持 ASAN / UBSAN 的 Linux C++ 编译器。Flutter、Java 和 Ninja 应在 PATH 中；使用 `flutter doctor -v` 检查 Android SDK。

```sh
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
sdkmanager --install "platforms;android-36" "build-tools;35.0.0" \
  "ndk;27.1.12297006" "cmake;3.22.1"
```

## 完整 Waydroid APK

```sh
bash scripts/build-waydroid.sh
```

脚本在临时副本中构建，不裁剪工作目录中的 ABI。它依次获取并适配原生依赖、编译 ARM64 guest 和 x64 host、获取 Xposed API、构建 Flutter APK、检查打包内容，将结果复制到 `build/zoot-waydroid-x64.apk`。

不需要预先放入游戏库、原模块二进制、手工编译的 `.so` 或个人配置。Gradle wrapper 由 Flutter 工具生成；`local.properties` 由本机 Flutter / SDK 环境生成，不从作者机器复制。

可选环境变量：

| 变量 | 用途 |
| --- | --- |
| `ANDROID_SDK_ROOT` / `ANDROID_HOME` | Android SDK 根目录，前者优先 |
| `ANDROID_NDK_ROOT` | 覆盖 NDK 路径 |
| `ZOOT_CMAKE` | CMake 可执行文件路径 |
| `ZOOT_NATIVE_DEPS_DIR` | BNM / Dobby 缓存，默认 `build/native-deps` |
| `ZOOT_BUILD_JOBS` | 原生并行编译数，默认 2 |
| `JAVA_HOME` | JDK 根目录 |
| `CXX` | 主机测试的 C++ 编译器 |

依赖脚本会恢复缓存中的固定上游提交，再应用本项目适配。对依赖的修改应写入 `scripts/configure-bnm.py` 或 `scripts/configure-dobby.py`。两个构建不要同时操作同一个依赖缓存。

## 原生代码迭代

```sh
bash scripts/build-native.sh
```

输出为 `android/app/src/main/jniLibs/{arm64-v8a,x86_64}/libakl_core.so`，并生成 guest 资产。后续仅重编 ARM64：

```sh
"$ANDROID_SDK_ROOT/cmake/3.22.1/bin/cmake" \
  --build build/native-arm64 --target akl_core --parallel 2
```

这一步不会更新 APK 或 guest 资产；交付前重新运行完整构建。

直接 ARM64 路径保留在源码中，可在原生构建后执行：

```sh
bash scripts/fetch-xposed-api.sh
flutter pub get
flutter build apk --release --target-platform android-arm64 -Pabi=arm64-v8a
```

ARM64 设备尚未完成本轮运行验证，当前 CI 以 Waydroid x64 包作为发布产物。

## 检查

```sh
flutter pub get
flutter analyze
bash scripts/test-translated-engine.sh
python3 scripts/verify-apk.py build/zoot-waydroid-x64.apk \
  --abis x86_64 --engine-abis x86_64 --guest
"$ANDROID_SDK_ROOT/build-tools/35.0.0/apksigner" verify build/zoot-waydroid-x64.apk
git diff --check
```

检查 APK 中的精确 ABI、ELF 架构、ARM64 guest、许可证资产、`resources.arsc` 压缩与对齐规则。主机测试不依赖 adb 或游戏，不执行 Houdini；运行结果不能替代演习。

当前 Dart 分析服务器在部分中文绝对路径下出现通信错误。遇到相同问题时，将干净源码复制到 ASCII 路径，重新 `flutter pub get` 后分析，不复制旧 `.dart_tool`。

## 签名

默认 release 构建使用当前环境的 Android debug key，密钥不入库。不同机器或 CI 生成的签名可能不同，只有相同签名的 APK 才能直接覆盖安装。对已有设备更新时，应使用原来的本地密钥重新构建。

## 运行诊断

正常运行不需要设置调试属性。修改模块配置后完全停止并重新启动游戏。

```sh
adb logcat -s AKL:I
```

有 root 的测试设备可临时设置：

```sh
adb shell su -c 'setprop debug.akl.probe_only 1'
adb shell su -c 'setprop debug.akl.metadata 1'
```

`probe_only=1` 只运行桥接 / Dobby 探针；`metadata=1` 输出详细元数据。两者需要重启游戏生效。测试后清空：

```sh
adb shell su -c 'setprop debug.akl.probe_only ""'
adb shell su -c 'setprop debug.akl.metadata ""'
```

功能验证使用演习；记录配置、游戏版本、结果画面和对应日志，区分“钩子已安装”与“效果已发生”。
