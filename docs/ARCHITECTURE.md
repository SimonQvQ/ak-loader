# 架构

控制台将开关、延迟、倍速与倍率写入 SharedPreferences("akl")。LSPosed 的 xposedsharedprefs 机制使游戏主进程可读取它；每次启动快照配置，修改后需重启游戏。

## 加载链路

1. XposedEntry 限定游戏主进程和已验证版本；模块关闭时退出。
2. 将所需库从本模块APK解到游戏自己的code_cache/akl。
3. x64侧加载host，运行时解析ART的NativeLoader/NativeBridge接口；必要时从已加载ELF解析被命名空间隐藏的导出，拒绝lazy转发库。
4. 以游戏ClassLoader的原生命名空间加载arm64 guest，借JNI形状跳板调用探针和引擎入口。
5. 探针执行一次Dobby重定位测试，检查预热函数改写和恢复。默认随后启动功能引擎。
6. guest等待已加载libil2cpp，安装BNM late-init。回调检查域与线程上下文，保护setup只执行一次。
7. BNM载入后通过当前导出元数据匹配完整方法签名，Dobby安装钩子。未找到目标时明确记录不可用。

arm64设备使用独立桥类的JNI路线；本次设备验证仅覆盖Waydroid x64/Houdini14。

## 一键结算

沿用原模块的Scheduler计数选择和WIN/false/null参数。当前游戏StartGame可通过GameMode回调延迟进入state2，因此在已核对的 `<StartGame>b__392_0` 原回调返回后执行一次结算。当前ObscuredInt通过运行时转换、拆箱、field_set_value写入，不猜结构、不复制旧版16字节。

## 依赖与布局

BNM/Dobby固定到脚本中的提交，下载放在忽略的build/native-deps中。本项目维护可重复应用的Android编译补丁与当前游戏布局适配。游戏2.7.81的Class计数偏移和vtable位置以当前导出访问器及实际调用指令为证，详见current-game-native-evidence.md。

自制hook.cpp/h已移除。BNM的自定义类/协程管理关闭，未使用的自定义List与finalizer模板初始化也关闭；这些能力不参与现有功能。

## 验证边界

主机测试验证跨架构协议和导出解析，不执行Houdini或游戏。ARM夹具验证Dobby指令重定位，不能证明具体战斗效果。真实演习证据见[本轮功能测试](feature-test-current.md)；用户确认的一键结算结果单独记录，不推广到特殊任务或奖励。
