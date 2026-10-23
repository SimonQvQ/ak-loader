# 参与开发

先阅读 [README](README.md)、[构建说明](docs/BUILDING.md) 和 [架构](docs/ARCHITECTURE.md)。当前游戏布局只验证到官服 2.7.81；新版本应建立独立证据和适配，更新入口版本检查。

## 修改位置

- 控制台和配置：`lib/main.dart`、Kotlin 的 `MainActivity.kt` / `XposedEntry.kt`。
- x64 加载桥：`waydroid_loader.cpp`、`translated_engine.*`、`loaded_export.*`。
- ARM64 战斗钩子：`ak_core.cpp`；独立于运行时的数值规则放在 `battle_policy.h`。
- 上游依赖适配：修改 `scripts/configure-*.py`，不要只修改 `build/native-deps` 缓存。
- 图标：修改 `design/icon/zoot-icon.svg` 并使用该目录生成器，保留源文件。

保持 JNI / managed 方法签名、返回值和隐藏 MethodInfo 参数完整。新增字段或 ABI 假设时附上当前版本证据。功能不可用时应明确记录，不能把探针成功当作战斗结果。

## 提交前

1. `dart format lib/main.dart`；C++ 使用仓库 `.clang-format`（本轮整理使用 clang-format 23.1.3）。
2. `flutter analyze` 和 `bash scripts/test-translated-engine.sh`。
3. 原生、Gradle 或打包变更执行 `bash scripts/build-waydroid.sh`。
4. 战斗行为变更逐项演习验证，补充 `docs/` 记录；说明未覆盖的特殊机制。
5. `git diff --check`，确认未包含密钥、个人配置、设备转储、游戏库或依赖缓存。

提交说明写清触发条件、修改后的行为、验证结果和仍未验证的范围。保留有来源与边界的历史实验记录，当前结论统一从 README 和文档入口进入。
