# System Tray Option Launcher 学习路线

## Goal

完成本路线后，你应能独立设计并实现一个由 `alure --tray-launcher` 启动、外观类似 rofi 的键盘优先选择器：先搜索系统托盘项，再进入该项导出的 DBusMenu、搜索并执行菜单动作；同时能解释进程模式、异步协议、状态机、QML/C++ 边界、Wayland 焦点和配置验证为何这样划分。

这里把“子命令”解释为 Alure 现有 CLI 风格中的互斥启动模式 `--tray-launcher`。若坚持使用真正的位置子命令 `alure tray-launcher`，需先重构当前拒绝全部 positional arguments 的 CLI 契约，不适合作为本功能的第一步。

## Target Language

- C++20：进程模式、控制器、异步 DBus、模型、生命周期与测试。
- Qt 6 Quick / QML：输入框、列表、布局、主题、动画与键盘交互。
- TOML：全部用户可见行为和外观的配置契约。

## Prerequisites

- C++：RAII、值语义、lambda 捕获、`std::unique_ptr`、基本状态建模。
- Qt：信号/槽、QObject 生命周期、`QVariantMap/List`、`QAbstractListModel` 基础。
- QML：属性绑定、FocusScope、TextField、ListView、delegate。
- 基础 DBus 概念：service、object path、interface、method、signal。
- 能阅读现有 `main.cpp`、`TrayService`、`TrayMenu`、`ClipboardHost`。

## Scope Boundary

首版只做：

1. 独立启动模式；不启动面板和无关服务。
2. 展示并过滤当前 StatusNotifierItem。
3. 对普通托盘项执行 Activate；对菜单型托盘项进入 DBusMenu。
4. 过滤当前菜单层、进入子菜单、返回、执行叶子动作。
5. Escape 按“清空查询 → 返回上层 → 关闭”处理。
6. 配置尺寸、输出、排序、匹配方式、关闭行为及单实例行为。

首版不做：应用程序启动器、命令执行器、插件系统、历史学习排序、任意 shell 命令、跨层级全文索引或新的托盘协议实现。

## Section Plan

### 01. 从“像 rofi”改写为行为契约与状态机

- Why now: 先把视觉类比转换为可测试状态，避免一开始堆 QML 控件后才发现数据、焦点和退出语义不清。
- Concepts: 用户旅程、状态/事件/效果、竞态、功能边界、验收条件。
- Exercise: 为选择器写状态转移表，并实现一个不依赖 Qt 窗口的纯 C++20 `reduce(state, event)` 原型。

### 02. 识别并复用现有托盘协议边界

- Why now: 状态机明确后，才能判断 `TrayService`、`TrayMenu` 哪些能力可复用，哪些只是面板专用表现。
- Concepts: StatusNotifierItem 与 DBusMenu 的职责区别、异步快照、稳定 ID、对象消失竞态、动作权限。
- Exercise: 画出从 watcher 到 launcher row、再到 DBus method 的数据流，并用现有 fixture 写协议适配测试草案。

### 03. 设计独立进程模式与最小依赖图

- Why now: launcher 不应构造全部 `Services` 或面板窗口；先固定启动边界可防止隐藏副作用。
- Concepts: `QCoreApplication`/`QGuiApplication` 选择、`QCommandLineParser` 互斥模式、`SingleInstance`、按需构造、退出码。
- Exercise: 修改 CLI 设计表和对象生命周期图，然后由你实现仅构造 ConfigStore、TrayService、engine、launcher host 的启动分支。

### 04. 用控制器建立 C++/QML 之间的展示模型

- Why now: DBus 原始字段不应直接决定所有交互；需要一个可测试的导航与过滤边界。
- Concepts: `QAbstractListModel` 或窄控制器、规范化 row、query、selection、breadcrumb、命令与查询分离。
- Exercise: 在两种方案间做选择并说明理由，然后实现最小角色集合和状态投影测试。

### 05. 构建键盘优先的 QML 界面

- Why now: 数据和动作契约稳定后再写界面，QML 只负责表现与输入映射。
- Concepts: `FocusScope`、active focus、TextField/ListView 键盘路由、currentIndex、不把协议逻辑写进 delegate。
- Exercise: 实现输入、上下选择、Enter、Escape 和空/加载/错误状态，验证鼠标与键盘得到同一动作。

### 06. 正确处理 Wayland layer-shell 表面与焦点

- Why now: 普通 offscreen 窗口成功不代表 Wayland 上能收到键盘；窗口角色和键盘策略是独立层。
- Concepts: overlay layer、exclusive zone `-1`、`KeyboardInteractivityExclusive`、输出选择、多屏、焦点丢失和销毁顺序。
- Exercise: 参考 `ClipboardHost` 提取 launcher host，完成 preview 与 Wayland 两条路径，并记录实际 Niri 验证。

### 07. 同步设计 TOML 默认值、校验和设置入口

- Why now: Alure 要求功能从第一版起可配置，不能把尺寸、排序、关闭行为散落在 QML。
- Concepts: 默认模型、类型/范围/枚举校验、热重载与仅下次启动生效、source-preserving settings。
- Exercise: 设计 `[launchers.tray]` 契约，补齐默认配置、自定义示例、无效值测试、文档和设置表单。

### 08. 分层测试、真实交互验证与收尾

- Why now: 最后组合 CLI、模型、协议、窗口和 QML，并明确自动测试不能证明的部分。
- Concepts: 纯状态测试、DBus fixture、QML offscreen、CLI 进程测试、computer-use/Niri 验证、资源清理。
- Exercise: 实现端到端场景矩阵，覆盖托盘项在搜索或动作期间消失、空菜单、禁用项、子菜单和重复启动。

## Suggested Architecture

```text
alure --tray-launcher
        │
        ▼
CLI mode selection ── ConfigStore
        │                  │
        ▼                  ▼
TrayLauncherHost      [launchers.tray]
        │
        ├── owns one launcher surface per probing/selected output
        └── exposes Shell.closePopup()

TrayService ── StatusNotifier watcher/host ── session DBus
     │
     ├── tray item snapshots / Activate
     └── TrayMenu ── DBusMenu navigation / Event
              │
              ▼
TrayLauncherController
  state + query + normalized visible rows + commands
              │
              ▼
TrayLauncher.qml
  layout + theme + animation + input mapping only
```

关键思路不是“复制一个 TrayMenu 再加 TextField”，而是：

1. 把 launcher 看成一个独立运行模式，而不是面板 popup。
2. 把托盘项选择与菜单项选择看成同一个导航状态机的不同页面。
3. 把 DBus 看成会延迟、失败、对象会消失的输入源，而不是同步数组。
4. 让 C++ 决定状态和效果，让 QML 表达布局、视觉与按键意图。
5. 先写配置和验收契约，再允许任何常量进入实现。

## Primary References Consulted

- Qt Quick keyboard focus: https://doc.qt.io/qt-6/qtquick-input-focus.html
- StatusNotifierItem specification: https://specifications.freedesktop.org/status-notifier-item/latest/status-notifier-item.html
- Layer shell protocol used by LayerShellQt: https://github.com/KDE/layer-shell-qt/blob/master/src/wlr-layer-shell-unstable-v1.xml
- rofi mode/manual concepts: https://davatorium.github.io/rofi/current/rofi.1/ and https://davatorium.github.io/rofi/current/rofi-script.5/

这些资料用于确认焦点、托盘协议和 rofi 的“模式/过滤/选择”思路；实现仍应沿用本仓库已有 Qt/C++ 边界，不复制上游代码。
