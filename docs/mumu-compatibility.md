# 是否需要仿造 MuMu 转译层

当前结论：为本模块的一键结算，现有 Waydroid/Houdini 已提供可用路径，无需先替换镜像里的转译层。

## 已观察到的事实

- 在这台 Waydroid 的游戏进程内，ART NativeLoader 使用游戏 ClassLoader 的翻译命名空间成功加载了外部 arm64 guest。
- 通过 NativeBridge 跳板执行了 guest JNI 形状入口。
- 预热后的 ARM64 ADRP 夹具在 Dobby 改写、调用原跳板、恢复后分别返回42/142/42。
- 游戏内 BNM 完成元数据初始化，一键结算的52个敌人计数与FinishGame调用已记录，用户已确认能用。

因此，此前“Waydroid 没有guest loader，不能加载任何外部arm64.so，所以必须仿MuMu”的绝对判断不成立。这台机器上早期的EM_AARCH64/EM_X86_64报错说明当次调用走了宿主链接器，不能证明翻译层缺少全部加载能力。

AOSP 的 NativeLoaderNamespace 本来就抽象了本机架构和翻译架构两种命名空间；两者由不同原始namespace类型表示。参考 [AOSP NativeLoaderNamespace 源码](https://android.googlesource.com/platform/art/%2B/main/libnativeloader/native_loader_namespace.h)。这说明ART接口的设计，不替代本机实测，也不证明所有Houdini移植版本都可用。

## 尚未做的事

没有运行或逆向指定MuMu/雷电版本，不能据此对它们内部所有guest linker补丁作比较。没有更换系统库、修改SELinux/镜像、伪造模拟器身份或安装另一转译层。

如果未来出现当前翻译层无法解决的具体缺口，应先明确可复现失败与所需接口，再在独立镜像测试其它实现；现有可用镜像不作为试验对象。
