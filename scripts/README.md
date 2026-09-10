# 安装与 systemd 用户服务

先安装项目 README 中的系统依赖。脚本不使用 sudo，不下载依赖，不修改 Niri
配置、现有 Alure 配置或其他桌面组件。

```sh
./scripts/install.sh          # 编译、安装至 ~/.local、注册 Niri 会话自启动；不立即启动
./scripts/install.sh --start  # 同上，并在当前 systemd 管理的 Niri 会话启动
```

选项：

- `--prefix /absolute/path`：安装位置，默认 `$HOME/.local`；支持空格。
- `--build-dir /absolute/path`：独立构建目录，默认项目下 `build-install`。
- `--jobs N`：并行编译数，默认 2，必须为正整数。
- `--no-service`：只构建、安装，不访问 systemd；不能与 `--start` 同用。
- `--help`：显示帮助。

使用 Release、系统依赖和 Ninja 构建。可执行文件与示例分别安装至
`PREFIX/bin`、`PREFIX/share/alure`；生成的服务位于
`PREFIX/lib/systemd/user/alure.service`。已有配置保持原样；缺失时使用内置默认值。

已安装后可单独注册：

```sh
./scripts/register-service.sh --prefix "$HOME/.local" # 注册但不启动
./scripts/register-service.sh --prefix "$HOME/.local" --start
```

注册脚本执行 `systemctl --user link`、`daemon-reload`、`enable alure.service`。
`WantedBy=niri.service` 将 Alure 加入 Niri 的启动依赖；服务在
`graphical-session.target` 就绪后启动，并随图形会话停止。须使用 Niri 的
显示管理器会话或 `niri-session`；直接执行裸 `niri` 不触发此注册关系。
`--start` 要求 Niri 服务已运行且用户管理器已有 `WAYLAND_DISPLAY`，脚本不会
擅自导入或修改全局会话环境。存在不同的用户自定义 `alure.service` 时停止并
报告路径，不覆盖它。重复注册同一安装可安全执行。

```sh
systemctl --user status alure.service
journalctl --user -u alure.service
systemctl --user restart alure.service     # 更新已运行实例后，由你显式重启
systemctl --user disable --now alure.service # 停止并取消自启动
```

脚本只管理 Alure 服务。通知接管仍须自行设置
`modules.notifications.behavior.server_enabled=true`，不会停用或替换现有通知服务。

参考：[Niri 官方 systemd 配置](https://niri-wm.github.io/niri/Example-systemd-Setup.html)。
测试使用临时 HOME 和 systemctl 替身验证注册、重复执行、失败路径和配置保留，
不操作当前桌面的服务。
