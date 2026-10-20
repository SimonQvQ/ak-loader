# 明日方舟 · 启动器（ak_loader）

一个完全重写的、**只面向明日方舟**的载入器。没有卡密、没有联网授权、没有第三方游戏支持，
也没有任何一处代码来自其它模块——从 Flutter 界面、LSPosed 注入到 arm64 行内 hook 全部新写。

## 结构

```
lib/main.dart                       Flutter 控制台（开关 / 延迟 / 倍速 / 启动）
android/app/src/main/kotlin/.../     MainActivity（通道）、XposedEntry（注入）、NativeBridge
android/app/src/main/assets/xposed_init     Xposed 入口声明
android/app/src/main/cpp/            原生引擎
  ak_core.cpp                        il2cpp 定位 + UnityEngine.Time 劫持
  hook.cpp / hook.h                  自写 arm64 行内 hook
android/app/src/main/jniLibs/arm64-v8a/libakl_core.so   预编译产物
```

## 运行链路

1. 控制台把参数写进本模块的 `SharedPreferences("akl")`：
   `enabled` / `delay_ms` / `speed_x100`
2. 清单里声明 `xposedsharedprefs=true`，LSPosed 让游戏进程可读这份配置
3. `XposedEntry` 只认三个包（官服 / B服 / 台服），`isFirstApplication` 时起线程
4. 线程按 `delay_ms` 等待，然后从模块 APK 里解出 `lib/arm64-v8a/libakl_core.so`
   落到**游戏自己的** `code_cache`，`System.load` —— 不需要 root、不写游戏数据
5. 原生引擎 `dlopen("libil2cpp.so")`，用 il2cpp 导出的 C 接口走到
   `UnityEngine.Time.set_timeScale`，用自写 16 字节绝对跳转改写序言，实现倍速

## 关于"从零写的 hook"

`hook.cpp` 做的是：

- 校验序言 4 条指令里没有 PC 相对指令（B/BL、CBZ、TBZ、ADR、LDR literal），
  有就直接放弃并打日志，不做不可靠的指令重定位
- `ldr x16,#8 ; br x16 ; .quad dst` 作为 16 字节跳转桩
- 跳板 = 原序言 16 字节 + 同一跳转桩指回 `target+16`
- `mprotect` 改 RWX 写入、`__builtin___clear_cache` 刷 icache、再恢复 RX

`MethodInfo` 的布局各版本不同，`ak_core.cpp` 不硬编码偏移，而是把 0x00/0x08/0x18
三个候选指针逐个用"落在 il2cpp 代码段内"来校验，取第一个成立的，并打日志。

## 构建

```bash
./scripts/fetch-xposed-api.sh     # 取编译期 API（不打包进 APK）
./scripts/build-native.sh         # 从源码编译 libakl_core.so
flutter build apk --release
```

需要：Flutter 3.x、JDK 21（JDK 25+ 会在 AGP 的 jlink 阶段失败，见文末）、
Android SDK Platform 36 与 NDK 27.1.12297006（可用 `ANDROID_NDK_ROOT` 指定）。
release 走 debug 签名，自用足够。

详细设计见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 开源

GPL-3.0。原生引擎不链接任何第三方 hook 库（`llvm-nm -D` 只会看到 `JNI_OnLoad`
一个导出，依赖仅 libc/liblog/libdl）。

## 使用

1. 安装 APK，在 LSPosed 里启用并勾选明日方舟（作用域已预置三个包）
2. 打开 APP 调延迟与倍速
3. 启动游戏；`logcat -s AKL` 可看到引擎注入与 hook 日志

## 边界（如实说明）

- 只做倍速这一个功能点，没有原模块的卡密/多游戏/风控绕过那一套
- 需要设备已装 LSPosed（Android 10+ 官方 Xposed 不可用）
- 若命中"序言含 PC 相对指令"，hook 会安全放弃而不是硬改，日志里会说明
- 32 位进程目前不带 so，会打日志后退出（游戏主流为 arm64）

## 构建期踩过的坑

- `LoadPackageParam.modulePath` 在 api-82 里不存在 → 改用 `CodeSource` 定位模块 APK
- R8 会把 `XposedEntry`/`NativeBridge` 剥光 → 需要 `proguard-rules.pro` keep
- JDK 27 下 AGP 的 `JdkImageTransform` 必失败 → 用 JDK 21

## v0.2.0 — 战斗功能扩展

在倍速之外按 il2cpp 类名/方法名运行时挂钩（不依赖任何偏移，不引用原模块二进制）：

| 功能 | 目标 | 做法 |
|---|---|---|
| 倍速 | `UnityEngine.Time.set_timeScale` | 实参乘系数 |
| 无限部署费用 | `BattleController.SetCost(int,int)` | 两参覆写为 9999 |
| 生命不减 | `BattleController.ModifyLifePoint(int,obj,int,bool)` | 负值钳到 0 |
| 敌人停滞 | `BattleEnemy.MoveByRoute(float,bool*)` | 速度置 0 |
| 强制胜利 | `BattleController.get_gameResult()` | 先跑原逻辑，返回 3 |

未实现：秒杀（`BattleEntity.Init` 是 8 参混合结构，需要真机确认参义后才能安全改写，
贸然改会 Crash；要的话给我一次战斗 logcat，我按实测签名补上）。
