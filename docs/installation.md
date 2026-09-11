# 用户安装与 Niri 自启动

安装入口：`scripts/install.sh`。脚本安装到用户目录，不使用 sudo，不下载依赖。
默认注册 Alure 的 systemd 用户服务，并将其绑定到 `niri.service`，不立即启动。
`--start` 显式请求在当前 Niri 会话启动。已有配置不得被覆盖。

## 会话方式

通过显示管理器的 Niri 会话或 `niri-session` 启动 Niri。
它们使用上游 `niri.service`，负责导入 Wayland 环境并启动图形会话目标。
Alure 随 Niri 启动，在会话停止时由 systemd 停止；不需要修改 Niri KDL。
直接运行裸 `niri` 不等价于这个会话入口，不保证触发用户服务的自启动。

## 通知管理

Alure 的通知服务目前内置在 Shell 进程中，不是独立守护进程。
运行在 `alure.service` 中时，其生命周期、故障重启和日志均由 systemd 管理。
安装不能自动停止 mako/dunst 等现有通知服务，也不能抢占通知总线名称。

通知接管仍须显式配置：

```toml
[modules.notifications]
enabled = true

[modules.notifications.behavior]
server_enabled = true
```

已有对应表时编辑其中的值，不要重复添加表头。默认不接管通知。
若其他通知服务已持有 `org.freedesktop.Notifications`，Alure 会报告不可用，
不会替换它。应由用户自行决定是否停用原服务。

## 资料与边界

上游 Niri 推荐使用 `niri.service.wants` 绑定会话组件：
https://niri-wm.github.io/niri/Example-systemd-Setup.html

脚本的具体选项及验证结果随实现补充；本文不表示已经修改当前系统、
注册服务、启用通知或验证完整登录/注销生命周期。
