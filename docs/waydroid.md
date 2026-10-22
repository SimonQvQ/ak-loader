# Waydroid 适配与验证记录

## 当前状态（2026-10-22，v0.5.0 实验版）

已重写 x86_64 控制端：从游戏原生命名空间加载 arm64 guest，经 NativeBridge 跳板
调用探针和功能入口。专用包名为 `zoot-waydroid-x64.apk`，安装 ABI 为 x86_64；
arm64 执行端放在 APK 资产中。实现、状态码和构建方式见 [x86_64.md](x86_64.md)。

host / guest 的交叉编译、Kotlin 编译、模拟平台接口回归检查已通过。
模拟检查覆盖错误分支、探针协议、参数传递及并发重复启动保护，不等同于 Houdini 实测。
本会话的 ADB 套接字被沙箱拒绝，尚未验证实际桥接、hook 和战斗效果。
GitHub 写入连接此前返回 403，因此尚未触发新版 CI 或上传新版 APK。

Actions [#7](https://github.com/SimonQvQ/ak-loader/actions/runs/39619266417) 和 Release v0.4.0
只证明旧版安装包构建成功。它们不包含新 x86_64 控制端。

以下是旧加载路线的历史实测。`EM_AARCH64 instead of EM_X86_64` 证明那次调用走了
宿主链接器；独立 arm64 测试进程的 dlopen 成功也不能证明游戏进程里的 JNI 或 hook 成功。
目前不能据此宣称所有 Waydroid 路线都不可行，也不能宣称新路线已生效。

环境：Waydroid（x86_64 容器）+ Magisk Delta 26.3 + houdini 14.0.0 + Android 13 (API 33)。

## 一、LSPosed 安装

`magisk --install-module` 报 `Incomplete Magisk install`：magisk 的安装器会检查
`/data/adb/magisk`、`/data/adb/magisk/busybox`（X_OK）、`/data/adb/magisk/util_functions.sh` 三项
（二进制里 0x28b91 函数：`getuid` → 3 次 `access()` → 失败分支打印该串）。
本镜像里 `busybox` 是 0644，不可执行 → 补 `chmod 755` 即可让安装器启动。
之后安装器走 `/system/bin/sh` + `$MAGISTMP/busybox`，需要把 busybox applet 全量铺开：

```sh
T=$(magisk --path)
mkdir -p $T/busybox
cp -f /data/adb/magisk/busybox $T/busybox/busybox && chmod 755 $T/busybox/busybox
cd $T/busybox && $T/busybox/busybox --list | while read a; do ln -sf busybox "$a"; done
chmod 755 /data/adb/magisk/busybox
magisk --install-module /sdcard/lsposed.zip
```

Zygisk 开关（settings 表里没有该键就是没开）：

```sh
magisk --sqlite "insert or replace into settings (key,value) values('zygisk',1)"
```

重启容器后 `logcat | grep zygisk` 应出现 `zygisk64: replaced com/android/internal/os/Zygote#nativeForkAndSpecialize`，
`/data/adb/lspd/` 生成，`lspd` 进程起来 = 激活成功。

LSPosed 的模块状态不在 xml 里，而在 `/data/adb/lspd/config/modules_config.db`（带 -wal/-shm），
表 `modules(mid, module_pkg_name, apk_path, enabled)` 与 `scope(mid, app_pkg_name, user_id)`。
用逐条 SQL 改 `enabled=1` 并写 scope，就能免点 UI 直接启用模块（改完清掉 wal/shm 再重启）。

## 二、本模块（dev.ak.ak_loader）

- 装 APK 后在 LSPosed「模块」页应显示「明日方舟启动器 v0.3.x」并已启用。
- scope 需要包含 `com.hypergryph.arknights`（本模块只在该包内工作）。
- 启动器 UI：载入引擎开关 / 延迟注入 / 倍速 / 无限部署费用 / 生命不减 / 敌人停滞 / 强制胜利 / 攻击倍率。

## 三、原版（仙喵阁 1.2.28）实测：同样跑不起来

把原版装进同一个容器（`com.xg.gamebox`，primaryCpuAbi=arm64-v8a），LSPosed 能正常加载它的
`com.xg.gamebox.XposedHooks`，`initZygote` / `检测到需要注入的进程` 日志都正常。它读
`/sdcard/Android/data/com.hypergryph.arknights/files/xmg/xmg.cfg.json`，把 soPath 里的
lib*.so 换成 `libxmg_core.so` 后 `System.load`：

```
XMG-Lsp : [UnsatisfiedLinkError]加载so失败: dlopen failed:
  ".../lib/arm64/libxmg_core.so" not found
```

把它的 `libxmg_core.so` 手工复制进游戏自己的 `lib/arm64/` 再试，内核链接器直接拒绝：

```
XMG-Lsp : [UnsatisfiedLinkError]加载so失败: dlopen failed:
  ".../lib/arm64/libxmg_core.so" is for EM_AARCH64 (183) instead of EM_X86_64 (62)
LSPosed-Bridge: Crash unexpectedly: UnsatisfiedLinkError:
  No implementation found for void com.xg.gamebox.NativeBridge.loadCommonAndRun(...)
```

这次实测中，原版的这条加载路线未能加载 arm64 库；不能据此断言所有 NativeBridge 加载路线都不可行。

## 四、x86_64 适配：能加载、会明确报原因、不再崩

`scripts/build-native.sh` 现在同时产出 `arm64-v8a` 与 `x86_64`；模块按进程 ABI 选库，
arm64 加载失败自动回退 x86_64。x86_64 引擎启动后立刻检测并停用功能：

```
I AKL : akl_core 已加载
E AKL : 本进程是 x86_64：运行的是翻译层里的 arm64 游戏，原生 hook 无法生效，功能停用。
E AKL : 请把模块装在 arm64 真机上；此构建存在的意义只是让引擎能加载并给出原因。
```

之前在 x86_64 上会 SIGSEGV（`rip` 落在未映射地址），原因是引擎继续走 il2cpp 定位流程；
现在非 arm64 直接早退，只做诊断，不再碰内存。

## 五、验证结果与限制

在本环境下已验证：

```
I/LSPosed-Bridge: Loading legacy module dev.ak.ak_loader from /data/app/.../base.apk
I/LSPosed-Bridge:   Loading class dev.ak.ak_loader.XposedEntry
I/LSPosed-Bridge: [AKL] 进入 com.hypergryph.arknights
I/LSPosed-Bridge: [AKL] 模块 APK = /data/app/~~.../dev.ak.ak_loader-.../base.apk
```

即 LSPosed → 模块类加载 → 进入游戏进程 → 定位模块 APK，全链路通。

**限制**：Waydroid 是 x86_64 进程，明日方舟是 arm64，靠 houdini 翻译运行。
`System.load()` 一个 arm64 so 会被内核链接器拒绝：

```
dlopen failed: ".../libakl_core.so" is for EM_AARCH64 (183) instead of EM_X86_64 (62)
```

x86_64 版当前没有 arm64 hook 后端，加载后会主动退出。
Waydroid 的 arm64 引擎桥接路线仍需另行实测，见文首。

## 六、真机测试步骤

1. 装 LSPosed（Zygisk 版）+ 本模块 APK，模块页启用，scope 勾明日方舟。
2. 打开启动器，设延迟 5s、倍速 x2，开一个功能。
3. 启动游戏进一局，`adb logcat | grep AKL` 应看到
   `[AKL] 引擎已注入 mask=0x.. speed=x2.0 atk=x1.0`。
4. 若报 `定位不到模块 APK`，把日志发我——说明该 Xposed 实现既不暴露 codeSource、
   maps 里也没有 APK 路径，需要再加一条回退。

## 旧 nativeLibraryDir 加载路线的补充实测（2026-10-21）

证据链（`/data/adb/lspd/log/verbose_*.log`，游戏进程 pid 10127）：

```
I/LSPosed-Bridge] Loading legacy module dev.ak.ak_loader from /data/app/.../base.apk
I/LSPosed-Bridge]   Loading class dev.ak.ak_loader.XposedEntry
I/LSPosed-Bridge] [AKL] 进入 com.hypergryph.arknights
I/LSPosed-Bridge] [AKL] nativeLibraryDir 里的 so 加载失败（dlopen failed:
    ".../lib/arm64/libakl_core.so" is for EM_AARCH64 (183) instead of EM_X86_64 (62)）
I/LSPosed-Bridge] [AKL] arm64 so 加载失败（... 同上），回退 x86_64
```

说明：

1. LSPosed 注入链路本身是好的：模块类被加载、`handleLoadPackage` 执行、日志打到游戏进程。
2. 但明日方舟在 Waydroid 里是 **x86_64 进程**（arm64 的 libil2cpp.so 由 houdini64 翻译执行，见
   `/proc/<pid>/maps` 里 `lib/arm64/libil2cpp.so`），所以 `System.load()` 走的是 **x86_64 bionic linker**，
   它按 ELF machine 校验，直接拒绝 EM_AARCH64 的 .so。
3. 单独验证：arm64 ELF 可执行文件在本环境能跑，`dlopen("/data/local/tmp/libakl_core.so")` 返回 OK
   （`dlsym JNI_OnLoad = 0x1acddb20`）。该测试只在独立进程加载库并查找地址，
   没有调用 `JNI_OnLoad`，也没有验证游戏进程内的 JNI、il2cpp 或战斗功能。
4. 上述日志未覆盖继承游戏原生命名空间的 ClassLoader 加载路线；
   它证明旧路线失败，尚不能证明 Waydroid 的所有桥接路线都不可行。

当前尝试及验收条件见文首。新的加载路线仍需在 Waydroid 内实测。
