# 本轮功能测试（2026-10-22～23）

所有自动操作均为演习，未使用理智。标为“日志确认”的项目不替代画面验证。

## 第一批：费用、生命、倍速

环境：官服2.7.81，Waydroid Android13 x64/Houdini14，PID6501，mask0x7，speed200，damage10。
YW-3「破手铐」演习。

- 初始费用界面99，生命3。实际部署凛御银灰和斩业星熊后可部署数8→7→6，费用仍显示99。
- 多次敌人进入目标点，日志反复记录 `HIT life requested=-1 applied=0 side=0 result=0`，画面生命保持3。
- 倍速日志命中 `requested=1 applied=2`；部署减速时命中 `requested=.2 applied=.4`。当前证据确认setter调用与参数，尚未读取Unity实际getter或作墙钟速率对比。
- 本轮一键结算/强制胜利均关闭，最终正常到达三星结果页。

费用界面可能对大于99的内部值封顶，不能仅凭两次部署后仍显示99证明永久回填。后续加入ModifyCost变化后的回填与实际GetCost记录，再验证长期语义。

证据：build/x64-test-logs/basic-3-battle.txt、cost-after-deploy.png、basic-3-result.png。

## 后续轮次

伤害实现按DoApplyModifier作用域内的finalDelta缩放，保留特殊固定扣1血机制的覆盖限制。后续证据按轮次记录如下。

## 第二批：路线停滞、费用回填、倍速getter、伤害x2

PID7663，mask0x2b，speed200，damage20；YW-3演习。

- 15:19:18 UTC开战，Unity get_timeScale实际读到2.000。选择干员的减速也实际读到0.400；暂停读到0.000。
- 路线回调输入0.0333被置0，连续样本位置保持(1,0)。在未部署前，敌人停在出生点、生命3、完成数0；部署后仅射程内敌人被击杀，左侧敌人仍堆在出生点。
- 娜仁图亚扣15费用，实际GetCost为84，回填后99，原ModifyCost返回true。
- 友方→敌方最终伤害x2：同一次原getter增量-36.1，缩放-72.2，实际HP8000→7927.8。后续-67→-134、-112→-224等，HP差均对应缩放值；说明缩放位于已计算的伤害增量，非面板攻击翻倍。
- 游戏整体保持运行，随后从演习暂停菜单主动放弃（返还0理智）。

证据：build/x64-test-logs/freeze-damage-battle.txt、freeze-after-deploy.png。

治疗/敌方伤害的实际排除样本、失败改胜利仍在后续轮次；纯策略测试已覆盖敌方→友方、治疗、非有限FP与有限溢出的分支，不能把主机测试冒充游戏样本。

## 第三批：敌方伤害排除、失败改胜利

2026-10-23，PID8535，mask0x32，speed100，damage20；17-4演习。昨日洄作战已结束，YW-3进入请求被游戏拒绝，因此改用主线。

- 一键结算、生命保护、停滞均关闭，实际部署斩业星熊并任由其他路径漏怪。
- 敌方→友方：`damage=1 sides=2->1 eligible=0 delta=-93.5 scaled=-93.5 hp=3228->3134.5`；后续-91.2242和-7.5也保持原值。这些是倍率x2开启时的实际受击样本。
- 生命由3降至2，最终耗尽；同步31个敌人四项计数，调用`FinishGame(WIN,false,null)`。结果画面为两星“行动结束”。该功能改变胜负，不恢复漏怪后的星级。
- 非伤害modifier的+1保持+1，敌人满血时HP仍100。这只能证明该modifier没有被缩放，不能作为实际回血量证据。随后增加满血空回复的日志过滤，再验证实际治疗。

证据：build/x64-test-logs/win-damage-battle.txt、fail-win-two-star-result.png。

## 第四批：最终完整APK的治疗排除

2026-10-23，0.6.1+7完整APK，PID2119，mask0x26，speed100，damage20；17-4演习，部署斩业星熊和编队内医疗干员。费用、生命保护开启；一键结算、失败改胜利、停滞关闭。

- 友方自回复：`damage=0 sides=1->1 eligible=0 delta=5.088 scaled=5.088 hp=3222.5->3227.588`。实际HP增加值等于原增量。
- 医疗：`damage=0 sides=1->1 eligible=0 delta=476 scaled=476`；受击后HP3058→3228，保留最大HP封顶。
- 敌方回血：`damage=0 sides=2->2 eligible=0 delta=800 scaled=800 hp=1616.96->2416.96`。实际增加800。
- 同轮普通伤害仍按x2缩放，例如友方→敌方-595.76→-1191.52，HP4000→2808.48；敌方→友方-7.5保持-7.5。
- 因满血空回复较多，日志仅过滤非伤害且HP没有变化的样本；此调整只影响诊断输出，不改变modifier处理。

证据：build/x64-test-logs/healing-final-apk.txt、healing-final-apk.png。

## 第五批：七项全开组合与交付状态

2026-10-23，0.6.1+7完整APK，PID2991，mask0x7f，speed200，damage20；17-4演习。启动前清空debug.akl.probe_only、metadata、damage_trace，使用默认功能引擎路径。

- 新进程内ADRP夹具42→142→42，安装七项相关钩子。
- 开战回调同步31个敌人四项计数，触发WIN，画面为三星“行动结束”。
- 同步结算发生于原StartGame返回前。后续没有再次应用战斗倍速或回填费用；菜单`battle=0 requested=1 applied=1 observed=1`，确认速度回到1.0。
- 本轮验证组合启动/结算和速度恢复；各战斗功能效果以此前独立轮次为证，开场即结算不能替代那些样本。
- 测试结束已关闭游戏进程，保留一键结算；费用、生命、停滞、失败改胜利关闭，速度100、伤害10、延迟5000ms、引擎启用。

证据：build/x64-test-logs/all-features-final-apk.txt、all-features-three-star-result.png。

完整构建、APK签名、精确ABI/ELF/资源对齐/许可证资产检查、主机ASAN/UBSAN桥接与FP策略检查通过；Flutter分析在ASCII路径的干净依赖副本中通过。中文路径直接分析会触发当前Dart分析服务器通信异常，未改动本地SDK规避。

本轮已验证包的本地文件名：zoot-waydroid-x64-v0.6.1.apk（以下摘要对应该测试产物，CI 重建包可能不同）
SHA256：c19a87c744932d7556444d1347f9407ccfd08306dcdb997b62390402ff2145bd

测试中曾发生Waydroid会话整体停止、ADB离线；恢复会话并停止构建守护进程后，重新安装最终完整APK，完成第四、第五批。该事件没有作为模块的崩溃结论，亦未修改镜像或转译层。
