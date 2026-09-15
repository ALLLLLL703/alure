# 01 从“像 rofi”改写为行为契约与状态机

## Goal

学会先把模糊的界面类比转换成状态、事件、效果和验收条件，再开始写 Qt/QML。

## Core Concepts

- **需求不是控件清单。** “像 rofi”只说明大致交互感受，不能回答数据从哪里来、Enter 做什么、异步失败时停在哪个状态。
- **状态（State）**描述当前已经成立的事实；**事件（Event）**描述刚发生什么；**效果（Effect）**描述需要对外执行什么。
- **纯状态转移**不直接调用 DBus、创建窗口或修改 QML，因此容易穷举测试。
- **异步结果必须带代际或请求标识。** 用户返回上层后，旧的 DBus 回包不能覆盖新页面。
- **先定义退出语义。** launcher 的 Escape、失焦、第二次启动、执行动作后的行为不能由不同控件各自猜测。
- **UI 状态与领域状态分开。** `currentIndex` 是视图选择；“当前浏览托盘列表还是某个菜单层”是应用状态。

## 1. 先把名词拆开

这个功能实际包含两个不同协议层：

1. **托盘项页**：来源是 `TrayService::items()`，一行代表一个 StatusNotifierItem。
2. **菜单页**：来源是 `TrayMenu::items()`，一行代表当前 DBusMenu 层的 item。

它们可以共享“输入框 + 可过滤列表”的外观，但 Enter 的含义不同：

| 当前页面 | 当前行 | Enter 的效果 |
|---|---|---|
| 托盘项页 | `ItemIsMenu=false` | 请求 `Activate`，成功发出请求后关闭 launcher |
| 托盘项页 | `ItemIsMenu=true` | 请求打开 DBusMenu，进入 loading |
| 菜单页 | submenu | 请求加载子菜单，不关闭 |
| 菜单页 | enabled leaf | 发送 DBusMenu `Event("clicked")`，成功后关闭 |
| 菜单页 | disabled/separator | 不产生效果 |

这张表比“做一个 ListView”更接近真正的实现任务。

## 2. 最小状态模型

先不用 Qt 类型写一个教学模型：

```cpp
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

enum class Page { TrayItems, MenuItems };
enum class Phase { Loading, Ready, Error };

struct Row {
    std::string id;       // 稳定协议 ID，不是当前数组下标
    std::string label;
    bool enabled = true;
    bool submenu = false;
    bool itemIsMenu = false;
};

struct LauncherState {
    Page page = Page::TrayItems;
    Phase phase = Phase::Loading;
    std::string query;
    std::vector<Row> rows;
    std::vector<std::string> menuPath;
    std::uint64_t generation = 0;
    std::string error;
};
```

应记住的语义：

- `rows[index]` 会因过滤、刷新而变化，所以动作必须携带稳定 `id`，不能只携带 index。
- `menuPath` 表示领域导航路径；QML 的 `ListView.currentIndex` 不应承担这个职责。
- `generation` 在开始新请求、返回上层或关闭时递增；回包只在 generation 仍匹配时生效。
- `Loading` 不是空列表的别名。空列表可能是成功结果，错误也不能伪装成空列表。

## 3. 事件与外部效果

事件应描述“已经发生”，而不是藏着具体 Qt 调用：

```cpp
struct QueryChanged { std::string text; };
struct Confirmed { std::string stableId; };
struct EscapePressed {};
struct TraySnapshot { std::uint64_t generation; std::vector<Row> rows; };
struct MenuLoaded { std::uint64_t generation; std::vector<Row> rows; };
struct RequestFailed { std::uint64_t generation; std::string message; };

using Event = std::variant<
    QueryChanged, Confirmed, EscapePressed,
    TraySnapshot, MenuLoaded, RequestFailed>;

struct ActivateTray { std::string id; };
struct OpenMenu { std::string trayId; };
struct OpenSubmenu { std::string menuItemId; };
struct GoBack {};
struct CloseLauncher {};

using Effect = std::variant<
    ActivateTray, OpenMenu, OpenSubmenu, GoBack, CloseLauncher>;
```

教学上可以让 `reduce` 返回“新状态 + 可选效果”：

```cpp
struct Transition {
    LauncherState state;
    std::vector<Effect> effects;
};

Transition reduce(LauncherState state, const Event& event);
```

真实 Qt 实现未必原样使用 `std::variant`。你可以在 QObject controller 中用 slots/signals 表达事件和效果，但应该保留同一个思维模型：**处理事件时先决定状态，再发起副作用；回包再作为新事件进入。**

## 4. Escape 是一条有优先级的规则

建议首版采用：

```text
if query 非空:
    清空 query
else if 当前在菜单页且可以返回:
    返回上一菜单层
else if 当前在菜单页:
    返回托盘项页
else:
    关闭 launcher
```

为什么不能直接写 `Keys.onEscapePressed: Shell.closePopup()`？因为那会让窗口组件替领域导航做决定，并使菜单返回、清空搜索和关闭互相冲突。

是否要采用这个顺序属于产品决策，必须成为 `[launchers.tray]` 的可配置行为或明确固定契约；不能让 TextField、ListView、Host 分别实现不同版本。

## 5. 异步竞态：把“晚到的正确结果”视为错误上下文

场景：

1. 用户在托盘项 A 上按 Enter，请求 A 的菜单，generation=7。
2. 请求未返回，用户按 Escape 回到托盘列表，generation 变为 8。
3. A 的菜单结果才返回。

这个 DBus 回包本身可能完全正确，但已经不属于当前页面。因此：

```cpp
if (reply.generation != state.generation) {
    return {std::move(state), {}}; // 丢弃陈旧结果
}
```

同类情况还包括：

- 托盘应用在过滤结果显示后退出。
- 配置重载关闭并重建窗口。
- 第二次子命令调用切换掉已有 launcher。
- 用户快速进入两个不同托盘项的菜单。

不要用“按钮 loading 时全部禁用”代替代际检查；禁用 UI 不能阻止 DBus owner 消失、窗口关闭或配置重载。

## 6. 从状态推导视图，而不是复制状态

可见行应由原始 rows、query 和排序策略推导：

```cpp
std::vector<Row> visibleRows(
    const std::vector<Row>& rows,
    std::string_view query,
    MatchMode mode);
```

首版可选择简单、不区分大小写的 substring；不要因为 rofi 支持复杂匹配，就立即实现 fuzzy scorer、历史权重和插件模式。只有在契约要求排序分数时，分数才应进入模型。

注意边界：

- separator 不应参与普通文本匹配后孤立显示。
- label 为空时需要可辨识 fallback（例如 Title 或明确的 unavailable label）。
- query 改变后，选择应落到第一个 enabled、非 separator 行，而不是机械设置为 index 0。
- 数据刷新后应尝试按稳定 ID 保留选择，而不是按旧 index。

## 7. 与当前仓库的对应关系

现有代码已经给出三个可复用方向：

- `src/app/main.cpp`：启动模式互斥、按模式只构造需要的对象。
- `src/services/TrayService.*`：托盘快照、Activate、ItemIsMenu 和 watcher/host 协作。
- `src/services/TrayMenu.*`：异步 DBusMenu 加载、子菜单栈、generation、owner 消失处理。
- `src/platform/ClipboardHost.*`：独立 layer-shell 表面、多屏探测、获得键盘和单次关闭生命周期。

但不要把它们机械拼接：

- `TrayMenu` 当前的父级栈是内部实现，launcher controller 需要明确自己能观察或命令哪些导航状态。
- `ClipboardHost` 的“光标附近卡片”不等于 launcher 的居中/指定输出策略。
- `PanelHost::createPopup()` 依赖 panel parent 和 anchor；独立 launcher 没有这个父表面。
- 构造完整 `Services` 会启动无关服务，违反最小副作用目标。

## 8. 第一阶段验收条件

在写 QML 前，应能用测试回答：

1. 托盘页确认普通项产生 `ActivateTray`。
2. 确认菜单型项产生 `OpenMenu` 并进入 loading。
3. disabled/separator 永远不产生执行效果。
4. Escape 按既定优先级清查询、返回、关闭。
5. generation 不匹配的成功和失败结果都被忽略。
6. 刷新后按稳定 ID 保留选择；ID 消失时选择合理回退。
7. loading、成功空列表、错误是三个不同状态。

## Common Failure Modes

- **从 QML 开始写。** 很快得到漂亮窗口，却无法稳定处理 DBus 延迟和导航。
- **把数组 index 当身份。** 一次过滤或刷新就可能执行错误项。
- **让 QML 直接拼 DBus 参数。** 协议和权限逻辑泄漏到表现层。
- **同步等待 DBus。** 阻塞 GUI event loop，输入和重绘一起停顿。
- **复用整个 `Services`。** launcher 启动时意外轮询音频、网络、更新等服务。
- **把“没有数据”都显示成空。** 用户无法区分加载中、真实为空和协议失败。
- **为了“像 rofi”过早实现插件/模糊评分。** 这没有解决首版真正困难的生命周期和动作正确性。

## Exercise

先不要实现 launcher 窗口。完成一个独立的 C++20 状态机练习：

1. 在你自己的学习分支中定义 `LauncherState`、事件和效果；可以采用上面的形状，但需自行决定 Escape 策略。
2. 实现纯函数 `reduce`，至少覆盖托盘页、菜单页、loading、error、query 和 generation。
3. 实现 `visibleRows`，支持 `substring` 与 `prefix` 两种策略；匹配不区分大小写。
4. 写表驱动测试，至少包含：
   - 普通托盘项与菜单型托盘项；
   - disabled leaf 与 separator；
   - query 改变后当前选择失效；
   - 请求期间返回上层，随后收到旧回包；
   - 选中项对应的托盘应用在确认前消失；
   - 成功但为空与请求失败。
5. 用 5～10 行说明两个设计选择：
   - 为什么效果不直接在 `reduce` 内调用 DBus？
   - 你选择按什么顺序处理 Escape，为什么？

约束：不创建 `QQuickView`，不调用真实 DBus，不加入 fuzzy scoring。这个练习的目标是证明交互语义，而不是提前实现产品。

## Optional Hints

- 可以用 `std::visit` 分派 `Event`，也可以先用多个重载函数降低一次性复杂度。
- `reduce` 的测试只比较状态和 effects，不需要 Qt event loop。
- 先让 `Confirmed` 携带 stable ID；之后再思考 controller 如何从当前可见行取得该 ID。
- 将大小写折叠封装成独立函数，避免过滤循环里混入页面状态。

## Review

- 为什么 `Phase::Loading` 不能用 `rows.empty()` 表示？
- 为什么 DBus 回包“成功”仍可能必须丢弃？
- 哪些事实属于 controller，哪些只属于 QML 的 ListView？
- 如果第二次执行 `alure --tray-launcher` 用作 toggle，它应产生 Event 还是直接销毁窗口？为什么？
