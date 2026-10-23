# 一键结算实现与验证

按用户 2026-10-22 的指示先完成一键结算，并已得到实战确认。其余功能随后实现及演习记录见 [本轮功能测试](feature-test-current.md)。本文保留一键结算的实现与首轮验证过程。

参考原模块的 BNM + Dobby 结构与结算流程；没有复用其授权、联网验证或二进制代码。依赖固定版本见 scripts/fetch-native-deps.sh，许可证见 third_party/licenses。

当前游戏 2.7.81 的实测元数据和本体字节要求：

- Assembly-CSharp / Torappu.Battle.BattleController。
- StartGame 为 void()，FinishGame 为 void(GameResult,bool,Action)。WIN=1；没有把星级数字当枚举传入。
- 原 StartGame 完成回调 `<StartGame>b__392_0` 返回后触发一次结算，沿用原模块的 `(WIN,false,null)` 参数。
- 结算前通过当前运行时 ObscuredInt 转换运算符、拆箱和 field_set_value 设置 Scheduler 四项完成/击杀计数；不复制旧版本猜测的16字节结构。
- BNM 使用当前游戏 class 尾部布局，保留相同方法/字段表头位置；profile 静态断言包括 method_count0x120、field_count0x124、rank0x132、vtable0x140。
- Houdini 路径只安装 late-init，避免已观察到崩溃的 il2cpp_init 入口钩子；检查 domain 非空并保护初始化只执行一次。

原始证据和边界见 [current-game-native-evidence.md](current-game-native-evidence.md) 与 [original-native-reference.md](original-native-reference.md)。元数据匹配和hook安装成功不等于实际通关，更不等于活动奖励领取。

运行验证（设备 UTC+8）：

- 22:36:32，PID4350，mask0x40，其余功能关闭。进入 YW-EX-6，日志确认当前总敌人数52，四项计数转换完成，开战回调后请求WIN，FinishGame(1,false,null)已执行。
- 用户随后明确反馈“可以用”“我打完了”，这是用户实战确认；不能把这条反馈延伸成所有活动任务或所有奖励都已验证。
- 第一份可用包保留为 build/zoot-instant-working.apk，SHA256 `48fbe119b031cee9e5bee3bfa8f4616176603ca9d8e0f75aad64f72b2e5ffab3`。它是native热替换测试包，版本号/图标仍是旧基底；正式完整构建随后生成。
- 日志：build/x64-test-logs/instant-ready-battle.txt。


完整构建已完成：build/zoot-instant-full.apk，版本0.6.0+6，包含新图标、默认引擎与2.7.81版本检查。APK结构验证通过。配置界面在ASCII临时副本中重建依赖后flutter analyze通过；直接在中文绝对路径运行时Flutter LSP曾因初始化JSON长度处理异常退出，这是工具路径问题，非Dart编译错误。
