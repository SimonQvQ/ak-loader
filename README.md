# zoot · 明日方舟载入器

[![build](https://github.com/SimonQvQ/ak-loader/actions/workflows/build.yml/badge.svg)](https://github.com/SimonQvQ/ak-loader/actions/workflows/build.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

Flutter 控制台 + LSPosed 模块，完整开放界面、Kotlin 入口、x64 加载桥、ARM64 执行端、依赖适配脚本、测试和图标源文件。执行端参考原模块的 BNM / Dobby 结构；本项目代码不包含卡密和联网授权逻辑。

**当前版本 0.6.1，已验证环境：官服 2.7.81 / Unity 2021.3.39f1，Waydroid Android 13 x86_64 + Houdini 14。** 游戏版本不匹配时跳过引擎。其他设备、服区及转译层版本尚未验证。

## 功能与验证

| 功能 | 当前行为及证据 |
| --- | --- |
| 一键结算 | 开战就绪后同步 Scheduler 计数并调用胜利结算；YW-EX-6 用户实战确认，17-4 演习开场三星结算 |
| 无限部署费用 | 部署扣费后回填；实际费用 84 → 99 |
| 生命不减 | 漏怪不扣目标点生命；演习多次漏怪仍保持 3 |
| 倍速 | 战斗倍率；实际读取 x2、部署减速 x0.4、暂停 0；结束后恢复 x1 |
| 敌人停滞 | 阻止敌人沿路线移动，保留攻击等机制；位置与画面均已确认 |
| 失败改为胜利 | 生命耗尽后进入胜利结果页；保留漏怪星级，实测两星 |
| 对敌最终伤害倍率 | 只缩放友方对敌方的普通最终伤害；x2 的实际 HP 差已验证，治疗、自回复、敌方伤害及敌方回血均保持原值 |

特殊固定扣血机制不参与伤害倍率。七项全开组合已通过 17-4 演习，详见[功能测试记录](docs/feature-test-current.md)。元数据匹配、探针通过和钩子安装成功分别代表不同阶段，奖励领取与特殊任务条件需单独验证。

## 使用

1. 在已配置 LSPosed 和 ARM 翻译层的 Waydroid 安装 x64 APK。
2. 在 LSPosed 启用 zoot，作用域选择明日方舟。
3. 打开 zoot，启用引擎并设置功能；从控制台启动游戏。
4. 修改配置后完全重启游戏。默认注入延迟为 5 秒。

构建产物可在 [Actions](https://github.com/SimonQvQ/ak-loader/actions/workflows/build.yml) 下载；版本发布见 [Releases](https://github.com/SimonQvQ/ak-loader/releases)。请核对版本，历史 Release 不代表当前实现。

## 从源码构建

Linux 环境：Flutter **3.47.7**、JDK **21**、Android SDK **36**、NDK **27.1.12297006**、CMake **3.22.1**、Ninja、Git、Python 3、curl 和 C++ 编译器。

```sh
git clone https://github.com/SimonQvQ/ak-loader.git
cd ak-loader
export ANDROID_SDK_ROOT="$HOME/Android/Sdk"
bash scripts/build-waydroid.sh
```

产物为 `build/zoot-waydroid-x64.apk`。安装 ABI 为 x86_64，ARM64 执行端位于 `assets/akl/libakl_guest.so`，运行时仍需要系统 ARM 翻译层。构建会下载固定提交的 BNM / Dobby 及编译期 Xposed API。

```sh
flutter pub get
flutter analyze
bash scripts/test-translated-engine.sh
```

主机测试使用 ASAN / UBSAN，覆盖桥接协议、错误处理、已加载 ELF 导出解析和战斗数值策略。ARM64 的 Dobby ADRP 夹具已在独立程序及游戏进程内执行；主机模拟测试不能代替演习。[完整构建、签名与调试说明](docs/BUILDING.md)。

## 代码结构

| 路径 | 内容 |
| --- | --- |
| `lib/` | Flutter 控制台 |
| `android/app/src/main/kotlin/` | 配置读写、LSPosed 入口、游戏启动、JNI 桥 |
| `android/app/src/main/cpp/waydroid_loader.cpp` | x64 host 与 ART NativeLoader / NativeBridge 连接 |
| `android/app/src/main/cpp/translated_engine.*` | 跨架构调度与协议 |
| `android/app/src/main/cpp/loaded_export.*` | 已加载 ELF 导出查找 |
| `android/app/src/main/cpp/ak_core.cpp` | ARM64 guest、元数据匹配与战斗钩子 |
| `android/app/src/main/cpp/battle_policy.h` | 纯数值策略，与游戏运行时分离 |
| `scripts/` | 构建、依赖适配与 APK 检查 |
| `tests/` | 主机回归测试 |
| `design/icon/` | ZOOT 图标 SVG、生成器和预览 |
| `third_party/` | 依赖来源、固定版本与许可证 |
| `docs/` | 架构、构建、证据及历史实验记录 |

运行链路为：LSPosed → x64 host → 游戏翻译命名空间 → ARM64 guest → BNM / Dobby。现有 Waydroid / Houdini 已能完成这条链路，无需为本模块更换转译层或改系统镜像。

## 开发文档

- [架构](docs/ARCHITECTURE.md) · [构建](docs/BUILDING.md) · [参与开发](CONTRIBUTING.md) · [更新记录](CHANGELOG.md)
- [当前游戏布局与数值证据](docs/current-game-native-evidence.md) · [一键结算实现](docs/one-click-implementation.md)
- [原模块静态参考](docs/original-native-reference.md) · [MuMu 与 Waydroid 的加载路径结论](docs/mumu-compatibility.md)
- [Waydroid 历史实验](docs/waydroid.md) · [x86_64 历史方案](docs/x86_64.md)

## 许可证与依赖

项目按 [GPL-3.0](LICENSE) 开源。BNM、Dobby 的固定提交、修改位置和上游许可证见 [third_party/README.md](third_party/README.md)；对应许可证也随 APK 分发。Flutter / Dart 依赖锁定在 `pubspec.lock`。

仓库开放本项目全部实现与适配脚本。依赖源码由固定提交获取，生成的 APK、密钥、设备数据及游戏 / 原模块二进制不进入源码仓库；分析记录保留其来源、摘要与验证边界。
