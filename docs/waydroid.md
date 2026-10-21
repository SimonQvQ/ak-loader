# 在 Waydroid 里装 LSPosed + 本模块（实测记录）

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

结论：**原版在这个容器里也加载不了原生库**。这不是我们实现的问题，是 x86_64 进程不能
`dlopen` arm64 so（houdini 只翻译 APK 里声明的 arm64 库，不接管运行期任意路径的 arm64 so）。
所以这是平台边界，换任何模块都一样。

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

x86_64 版引擎即使加载成功，也 hook 不到被翻译执行的 arm64 `libil2cpp.so`。
所以**原生 hook 必须在真机 arm64 上测**，Waydroid 只能验证注入链路和 UI。

## 六、真机测试步骤

1. 装 LSPosed（Zygisk 版）+ 本模块 APK，模块页启用，scope 勾明日方舟。
2. 打开启动器，设延迟 5s、倍速 x2，开一个功能。
3. 启动游戏进一局，`adb logcat | grep AKL` 应看到
   `[AKL] 引擎已注入 mask=0x.. speed=x2.0 atk=x1.0`。
4. 若报 `定位不到模块 APK`，把日志发我——说明该 Xposed 实现既不暴露 codeSource、
   maps 里也没有 APK 路径，需要再加一条回退。
