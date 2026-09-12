# Alure 改进与修复记录

## 范围与结论

本轮处理模块弹窗刷新、音量 OSD 生命周期、面板指针状态及窗口间距配置说明。
用户已明确选择“先修其余，记录预览限制”：**真正的任务栏窗口缩略图延期**；没有用图标、标题或信息卡冒充预览，没有新增预览配置或抓屏后端。

保留用户已有 `qml/ModuleStrip.qml` 改动；不改宿主 Niri／Alure 配置，不替换正在运行的桌面，不操作真实音量、网络或剪贴板。原生验证由父会话在隔离 Niri 中使用模拟程序完成，不能视为真实系统服务全覆盖。没有新增依赖，也没有自动下载或复制上游代码。

## 1. 模块弹窗刷新闪烁

**代码证据：** 原 `Service` 的 enabled、available、busy、diagnostic、state、items 都使用同一个 `changed` 通知。轮询开始／结束即使没有数据变化，也会使 QML 列表与通知的 `slice().reverse()` 重新求值；普通 QVariantList／JS 数组不是带稳定行 ID 的增量模型。仅在 publish 中比较相等不能隔离 busy 通知。

**已实现：**

- 为各属性提供独立且相等时不发出的通知；保留聚合 `changed`，兼容现有观察者；configure／fail 仍可靠清空失效数据。
- 服务已有数据时，后台轮询不把状态反复改为“Refreshing…”，也不因 busy 临时禁用交互控件。
- 通用与媒体弹窗只保留最后一个等待中的操作，在提供者回调返回后再发送；关闭／隐藏、配置变化、服务失效会取消，媒体目标播放器变化也取消。音量原有连续输入合并路径保留。已发送的命令不是可撤回事务。
- 自动测试实际发送滚轮事件，确认滚动范围、contentY 改变及停止，再检查轮询期间位置、行对象身份、嵌套焦点和 enabled 状态；不再仅通过直接写 contentY 证明滚动。

**边界：** 更新、通知、Wi-Fi、蓝牙的相同数据刷新与独立摘要变化有回归覆盖；真实行内容变化／排序变化仍可能重建 QVariantList delegate，尚未实现 keyed 增量模型。父会话原生检查确认 Updates 标题／状态稳定，但嵌套桌面的滚轮没有带来可观察列表位移，原因未定，**不能声称原生滚动保留已验证**。

## 2. 音量 OSD：更新当前窗口，而不是每次重建

原 `PanelHost::showOsd()` 每次先销毁全部 OSD 视图。现在配置／输出生命周期不变且 OSD 尚未到期时，直接更新现有根对象的 `snapshot` 并重启到期计时器，跨音量／亮度种类也复用。配置／输出重建、关闭或禁用仍按原策略清理；到期后重新出现会创建新窗口，不承诺永久单窗口。

不改变 `ui.osd` 默认值、输出选择、各类开关与 `duration_ms`；保留之前 BackgroundBlur 的表面销毁保护及“非空、表面外区域禁用模糊”修复，**不恢复有生命周期风险的 false-disable 调用**。

证据：

- `osdPassiveWindowAndDuration` 检查 QQuickView／根对象身份、最新快照、跨种类、末次更新起算的到期时间。
- 父会话原生 `volumeOsdBlurChurn`：退出 0，3 个测试结果通过，没有警告／Wayland 协议错误。
- 隔离模拟音量连续更新：旧版 11 次 OSD 通知对应 11 个 layer surface；本轮 10 次通知对应 **2 个 surface + 8 次原位更新**，退出 0，无协议错误。前两次通知相隔 1.987 秒，大于默认 1800ms，创建第二个窗口符合到期语义；不能写成“10 次仅建 1 个窗口”。
- 证据文件由父会话持有：`/tmp/alure-improve-session/osd-stage1-native.log`、`osd-continuous-{before,stage2}.log`、`osd-continuous-result.json`。

## 3. 自动隐藏与指针：局部证据，而非全局坐标猜测

`always` 本来就不隐藏；`dodge-windows` 无遮挡时可以显示；显式模块弹窗打开时父面板应被固定显示。需要区分这些策略与“离开后仍覆盖窗口”。原生基线的普通离开／右键后点外部两次检查均正常，最初的间歇性报告没有可靠复现。

本轮补充实际窗口局部 move／press／release 坐标与输入区域校验，越界运动可清除旧 hover。隐藏体仍使用有效为空的非空表面外 mask，不能把空 QRegion 当作禁止输入。

**原生反馈纠正了初版方案：** 990530d 曾在触发层 mask 缩小时直接丢弃 edge hover，父会话在默认 350ms 隐藏延迟下确认“物理边缘不动，刚显示就隐藏”。这不是物理离开，而是输入所有权正在移向主体。后续改为把最后一个触发层局部点映射到同一输出的面板矩形：在主体内则转移 hover 意图，在边缘留白桥内则保留 edge hover；触发层因 mask 缩小产生的 Leave 不能反向撤销转移，由后续主体事件／带位置的越界运动再清除，不扩大用户配置的延迟，不查询全局光标。

同时撤销弹窗关闭时盲目清除 hover 的方案；解除抓取本身不证明指针离开。测试分别检查“最后局部证据仍在主体内，关闭后保持”和“最后局部证据已在外部，解除 popup pin 后隐藏”。

父会话最终 handoff2 原生检查（显式私有 DBus、原 350ms 隐藏延迟、留白 0）通过：物理边缘静止超过 2200ms 仍显示，移开隐藏；第二次边缘唤出仍保持，真实任务右键弹窗打开，指针移开后 Escape 关闭弹窗并隐藏面板。交接期间第一次右键未命中、第二次成功，因此不声称所有首击时序均已解决。证据：`/tmp/alure-improve-session/handoff2-live.log` 与父会话 computer-use 截图。

**仍有边界：** 原生 popup grab 可能在指针物理位置未变时产生父窗口 Leave，也可能吞掉之后的事件。父会话以临时 3000ms 延迟观察到指针仍在主体上、Escape 关闭后隐藏；日志出现抓取期间父 Leave，尚未与原始版本对照，不能称为已证明的旧版回归。只有当前局部事件不足以可靠恢复真实位置，本轮不声称解决所有丢事件／抓取场景，也不新增全局轮询或推测性框架。多输出／分数缩放仍需进一步原生验证。

## 4. bar 与窗口间距：原计算正确，编辑对象容易选错

**父会话证据：** 顶部 main 厚度 40、边距 0、自动保留空间、`window_gap=0`；用户把 -256 设置在底部 auto-hide taskbar，而动态模式不预留空间，该值不会影响顶部。Niri 自己有 `layout gaps 16`。

在隔离 973×1176 输出上，现有代码：

| 顶部 window_gap | 请求 exclusive zone | 测得窗口高度 | 外观 |
| --- | ---: | ---: | --- |
| 0 | 40 | 1104 | 仍有 Niri 的 16px 间隔 |
| -16 | 24 | 1120 | 该配置下与顶部 bar 贴合 |

`window_gap` 是**当前面板额外保留量**，不是“希望最终看见的间距”。本轮保持默认 0、整数范围 -256..256 和原公式，只在设置页明确显示所选面板 ID／边／输出、模式和补偿是否有效，改进字段帮助、默认 TOML 注释与[配置文档](docs/configuration.md)。

父会话在显式私有 DBus、嵌套 Niri 的原生 Settings 中分别检查了两块面板：顶部 `top-audit` 显示自动预留空间、-16 调整量及 compositor gaps／struts 的区别；下拉选择底部 `tasks-audit` 后，帮助明确说明 auto-hide 不预留空间、window_gap／reserved screen space 被忽略，应选择紧邻不期望间距的 always-visible 面板。两项均有实际 computer-use 截图；只查看与切换选择，没有保存、应用或修改配置。日志：`/tmp/alure-improve-session/settings-isolated.log`。

- always 且自动或正数 zone：`max(0, base_zone + window_gap)`。
- `exclusive_zone=0` 不保留；auto-hide／dodge 发 -1、不保留，window_gap 被忽略；0 与 -1 的协议语义不同。
- 调整**已有顶部面板条目**的 `window_gap=-16` 可补偿本例的 Niri 16px，不应新增同 ID 面板，也不保证所有 compositor／strut／border／缩放组合完全相同。
- `margins` 控制屏幕边与 bar，`popup_gap` 控制弹窗，不是同一间距；补偿钳到 0 后不能继续保证简单正 zone 公式。
- 缺失字段用默认值；类型／范围非法拒绝并诊断。Save & apply 对启用配置监听的面板生效，`runtime.watch=false` 需重启。

已覆盖四边、默认／正负／极值／非法类型／越界、零 zone 及动态模式忽略补偿；未改宿主 Niri 设置。

## 5. 真正缩略图延期的依据

[Niri 26.04 发布说明](https://github.com/niri-wm/niri/discussions/3899)明确区分外部窗口枚举与尚未实现的 ext-image-copy-capture。知道 IPC 窗口 ID 并不等于拥有图像；[版本固定 IPC 源码](https://github.com/niri-wm/niri/blob/v26.04/niri-ipc/src/lib.rs)的 ScreenshotWindow 即使保存到文件也会写剪贴板。保存再恢复剪贴板同样会改变所有权并与应用竞争，不作为 hover 后门。全屏截图裁剪不能代表被遮挡／其他工作区的窗口，也会获取无关私密内容。

未来可重新评估 compositor 支持的标准 per-window capture；[ScreenCast portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)则是用户主动选择、授权的会话，不是无声任意 hover。两者都需要单独设计能力检测、资源上限与离开／锁屏／关闭／断连清理；本轮没有引入这些后端或配置。详见 [taskbar 文档](docs/taskbar.md)。

## 6. 额外发现与下一步改进

| 项目 | 证据／状态 | 建议 |
| --- | --- | --- |
| 默认配置测试过期 | 旧断言 11 模块，实际 13；clipboard 早已默认启用且懒加载，旧测试却期待 false。本轮改为校验完整模块名字集和真实默认值 | 保持命名断言，新增模块同步维护测试 |
| 全量 QuickUi 失败 | moduleIconTooltips 找不到 tile；clipboardCursorHost 缺少 icons provider 警告；clipboardView 出现未定义 delegate 数据及 SIGSEGV，属已有失败，未修复 | 对照真实服务 schema 更新 fixture，检查原始指针失效／空查找，使用 QPointer 并在重建后重取；不能据此直接断言生产 clipboard 崩溃 |
| SettingsCloseE2e 失败 | 私有 Xvfb 脚本依赖固定导航／Discard 坐标与短等待，原因未定位 | 保留真实快捷键／模态验证，同时稳定定位与就绪检测；不要把失败标成通过 |
| taskbar 文档过期 | 原文称 layout events 被忽略；代码实际给面板可见性更新几何而抑制展示列表通知 | 本轮纠正，并明确真正缩略图未实现 |
| 真实列表变化仍可重建 | 当前 QVariantList 不是 keyed incremental model | 后续以 QAbstractListModel 和稳定 ID 做精确更新；分别测插删、排序、选中、焦点与滚动锚点，不借本轮扩大重写 |
| 配置诊断可发现性 | 顶部／底部面板混淆导致错误归因 | 本轮已加所选面板提示；后续可做只读诊断视图显示 mode、实际 zone、mask／窗口交集与未知几何策略，不自动修改 compositor |

## 7. 有价值的模块方向（仅提案，未实现）

以下优先复用 QtDBus／现有 Niri 流，避免高频轮询和额外大型依赖；API 存在不代表本机服务或全部字段可用。

1. **电池健康与外设电量补强**：现有 battery 主要读 sysfs；可接 [UPower Device](https://upower.freedesktop.org/docs/Device.html) 的 Percentage、State、Capacity 与时间估算，用属性信号更新。拟定默认关闭新能力，设备选择、缺席隐藏、健康显示与百分比阈值可配置；区分健康容量与当前电量，不强制启动守护进程。
2. **计费网络／连通性徽标**：在 Wi-Fi 信息之外，用 [NetworkManager](https://networkmanager.dev/docs/api/latest/gdbus-org.freedesktop.NetworkManager.html) 的 Metered／Connectivity／State 标示未知、受限或计费连接。拟定默认关闭，SSID／IP 默认隐藏，单击默认无动作；不额外发起网络探测或扫描。
3. **正在共享屏幕指示器**：使用 Niri 26.04 的共享初始状态／事件，仅观察现有共享，不启动捕获。拟定默认关闭、应用名称默认隐藏、停止操作默认禁止；若未来允许停止须确认且按后端能力区分。不能冒充通用摄像头／麦克风隐私监视器，wlr-screencopy 状态有启发式限制。
4. **睡眠／关机阻止项查看器**：用 [login1 ListInhibitors](https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.login1.html) 解释谁阻止休眠。拟定默认关闭，手动或有界低频刷新（如 30 秒），进程号／理由默认隐藏，完全只读；不能为显示状态而自己申请 inhibitor。

每个提案若获批准，须同时接统一 TOML 模型、默认值、类型／范围诊断、位置／顺序／外观／交互配置、示例与生效方式；验证默认、自定义、非法、服务缺失与断连，不先硬编码再补配置。上述数值是未来方案建议，不是当前可用配置项。

## 8. 验证与提交

环境：Qt 6.11.2、Niri 26.04、KWindowSystem 6.30。构建使用系统依赖。父会话补充隔离边界：早期 GUI 启动虽然使用嵌套 Wayland，但继承了宿主 DBus，会话并非全部隔离；敏感模块已禁用／音量为模拟数据。OSD 二进制连续对比显式使用私有总线，后续 GUI 启动也改为显式私有总线。Settings 曾因继承总线激活已有宿主编辑器，未修改配置；不能声称此前所有客户端都使用私有 DBus。自动测试日志位于 `/tmp/alure-improve-*.log`，原生日志在父会话的 `/tmp/alure-improve-session/`，不是仓库中可长期下载的附件。

- `cmake --build build -j2` 通过。
- ConfigStore：69 通过；PanelHost：49 通过；Services 阶段回归：45 通过、1 跳过。
- 专项 UI 最后合并运行 24 通过：四模块刷新、真实滚轮偏移、媒体操作取消、OSD 身份／计时、面板配置提示、四边及 0/4/24 留白生命周期等。offscreen 的 mask 不支持警告意味着这些不是原生输入区域验证。
- 全量 CTest 从已知 8/11 到 9/11；QuickUi 与 SettingsCloseE2e 仍未通过。新增定时生命周期用例超过原 QuickUi 30 秒预算，单独调整到 90 秒后，完整运行仍暴露上述 fixture 失败／SIGSEGV，而非掩盖失败，**没有全量通过**。
- 原生 OSD 与间距证据见上；自动隐藏原生反馈驱动了所有权修正，弹窗抓取和滚轮尚有上述明确边界。
- 已提交阶段：`919f868` 弹窗通知／OSD 复用；`990530d` 面板局部输入／间距帮助及操作取消；`6b936f8` 修正所有权交接并补全报告。最终原生证据另有文档提交；以 Git 日志为准。无推送。

### 最终审查修正：延迟操作不能覆盖较新的请求

独立只读审查发现 P1：提供者忙时排队 A，变为空闲后 `Qt.callLater` 尚未执行，此时 B 直接发送却没有清除 A，随后 A 仍会发送。此前测试只证明延迟发送与取消，没有覆盖这个间隙。

本次仅在 `Popup.act`／`MediaPopup.dispatch` 的直接发送分支先清空等待项，再调用服务；保留原有 volume `setVolume`／`adjustVolume` 忙时交给服务合并的例外路径。没有改变已发送命令的取消语义，也未改动面板／原生输入处理。

- 新增 `popupImmediateActionSupersedesDeferred` 四个 fixture 数据行：通用与媒体弹窗，分别覆盖 B 发送后仍空闲、B 忙到延迟回调之后两种情况；检查只发送 B、参数及媒体目标未丢失，B 完成后也不重放 A。
- 修复前专项运行四行均复现 `actionCount=2` 而非 1（2 个初始化／清理通过，4 失败）。初版通用 fixture 同时缺少包名造成 QML 警告，已补充 fixture 字段；修复后检查无 QML 警告。
- `cmake --build build --target ui_tests -j2` 通过。显式私有 DBus、offscreen 专项命令：`env -u ALURE_TEST_NATIVE_WAYLAND dbus-run-session -- build/tests/ui_tests popupImmediateActionSupersedesDeferred popupStableRefresh mediaCardControls volumeContinuousDrag volumePendingFeedbackIsNotObserved volumeBackendCapabilities`，**15 通过、0 失败**（含初始化／清理）。覆盖新增顺序回归、旧延迟／取消、刷新滚动／焦点及连续音量输入。
- 本次日志：`/tmp/alure-improve-recovery-{build,red,ui}.log`。此前专项 UI 24、ConfigStore 69、PanelHost 49、Services 45 通过／1 跳过仍是保留日志结果，并非本次重跑。**全量 CTest 仍保留 9/11 失败结论，本次未重跑**；QuickUi fixture／SIGSEGV、SettingsCloseE2e，以及原生 popup grab 丢事件／首击／滚轮边界均未宣称解决。
- 此修正待父会话独立复审；用户 `ModuleStrip.qml` 保持与备份逐字节相同且不提交，本地工具文件不动。本次不启动原生 UI，不触碰宿主配置／设备，不安装或推送。
