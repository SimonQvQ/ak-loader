# 架构说明

## 三层分离

```
┌──────────────────────────────┐
│ Flutter 控制台（本模块进程）   │  只写配置，不做任何注入
└───────────────┬──────────────┘
                │ SharedPreferences("akl")
                ▼
┌──────────────────────────────┐
│ XposedEntry（游戏进程内）      │  延迟 → 解 so → System.load → 调 JNI
└───────────────┬──────────────┘
                │ JNI: start(speedX100)
                ▼
┌──────────────────────────────┐
│ libakl_core.so（原生引擎）     │  定位 il2cpp → 找方法 → 装 hook
└──────────────────────────────┘
```

这样切分的原因：控制台崩了不影响游戏，引擎崩了只影响当次启动；配置与执行解耦，
不需要在游戏进程里跑任何 UI 代码。

## 配置传递

`AndroidManifest.xml` 里 `xposedsharedprefs=true` 让 LSPosed 把模块的
`shared_prefs/akl.xml` 对目标进程可读，引擎侧用 `XSharedPreferences` 读，
**不需要 root，也不需要往游戏目录写文件**。

## so 的落地

模块 APK 里的 `lib/arm64-v8a/libakl_core.so` 不能直接从别的应用私有目录 `System.load`。
`XposedEntry` 用 `ZipFile` 把它解到**游戏自己的** `codeCacheDir/akl/`，再 `System.load`。
既不污染游戏数据目录，也不依赖模块自身的 lib 提取。

模块 APK 路径来自类加载器的 `CodeSource`（api-82 没有 `LoadPackageParam.modulePath`），
并用"能当 zip 打开且含目标条目"双重校验。

## 行内 hook（hook.cpp）

arm64 上的最小可靠改写：

```
目标序言 16B ──改写后──▶   ldr x16, #8
                          br  x16
                          .quad 我们的函数        (共 16 字节)

跳板 16B + 16B         ─▶  原序言 16B
                          ldr x16, #8 / br x16
                          .quad 目标+16
```

流程：

1. 读取序言 4 条指令，逐条判断是否 PC 相对（B/BL、CBZ/CBNZ、TBZ/TBNZ、ADR/ADRP、LDR literal）
2. 命中任意一条 → **直接放弃**，打日志返回 false，不做不可靠的指令重定位
3. `mmap` 一页 RW，写入"原序言 + 跳回桩"，再 `mprotect` 成 RX
4. `mprotect` 目标页 RWX → 写跳转桩 → `__builtin___clear_cache` 刷 icache → 恢复 RX

第 2 步是刻意保守的：重定位出错会静默改坏游戏逻辑，宁可这一版不生效也不乱改。

## 方法定位（ak_core.cpp）

不硬编码偏移：

1. `dlopen("libil2cpp.so")`，`dl_iterate_phdr` 取 PF_X 段区间作为"合法代码地址"判据
2. `il2cpp_domain_get_assemblies` 遍历程序集，对每个 image 调
   `il2cpp_class_from_name(image, "UnityEngine", "Time")`
3. `il2cpp_class_get_method_from_name(klass, "set_timeScale", 1)` 拿到 `MethodInfo*`
4. `MethodInfo` 布局随 Unity 版本变化，因此把 `+0x00/+0x08/+0x18` 三个候选指针
   逐个做"落在 il2cpp 代码段内"校验，取第一个成立的，并打印判定依据
5. 装 hook；失败则日志说明原因，不影响游戏正常运行

## 与市面模块的差异

| 维度 | 常见商业模块 | 本工程 |
|---|---|---|
| 授权 | 卡密 / 设备绑定 / 联网校验 | 无，纯本地开关 |
| 覆盖 | 多游戏聚合 | 只认明日方舟三个包 |
| 技术栈 | 常见 Dobby/Substrate 等第三方库 | 全部自写，仅依赖 libc/liblog/libdl |
| 行为 | 大量反射 + 主动对抗检测 | 只做一次方法 hook，不做对抗 |

## 已知限制

- 只实现倍速，未做功能面板
- 32 位进程不带 so（会打日志退出）
- 序言含 PC 相对指令时 hook 不生效（这是设计选择，不是 bug）
- 需要 LSPosed（Android 10+ 官方 Xposed 不可用）
