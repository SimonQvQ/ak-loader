# 第三方依赖

| 依赖 | 固定版本 | 用途 | 上游许可证 |
| --- | --- | --- | --- |
| [BNM-Android](https://github.com/ByNameModding/BNM-Android) | `502928771983d29e37f28c78f28823dfb775a3aa` | IL2CPP 元数据与运行时载入 | [MIT](licenses/BNM.txt) |
| [Dobby](https://github.com/jmpews/Dobby) | `5dfc8546954ce3b3198132ab13fddb89ee92cdd7` | ARM64 重定位与钩子 | [Apache-2.0](licenses/Dobby.txt) |

`scripts/fetch-native-deps.sh` 下载上述精确提交到 `build/native-deps`，然后应用 `scripts/configure-bnm.py` 和 `scripts/configure-dobby.py`。依赖及适配代码均可由开源构建脚本取得，不依赖未公开的预编译引擎。

## 本项目的适配

- BNM：选择 Unity 2021.3.39 配置、使用 Dobby、关闭未用到的自定义类和协程能力；为当前游戏适配 Class 布局；使用带域与线程检查的一次性 late-init 路径。
- Dobby：修正固定上游提交在当前 Android / ELF 工具链下的头文件、运行时字段和 ARM64 汇编引用；开启所需构建选项。
- 两份许可证原文保存在 `licenses/`，同时复制到 `android/app/src/main/assets/licenses/` 随 APK 分发。

## 其他依赖

Flutter / Dart 包由 `pubspec.yaml` 声明、`pubspec.lock` 锁定；其许可信息随 Flutter 生成的开源许可资源分发。编译期 [Xposed API 82](https://api.xposed.info/) 由 `scripts/fetch-xposed-api.sh` 获取，使用 `compileOnly`，运行时由 LSPosed 提供。

原模块只作为算法与结构的分析参考。参考记录见 [original-native-reference.md](../docs/original-native-reference.md)，本仓库不分发其二进制或授权实现。
