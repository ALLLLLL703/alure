# TODO

- [x] 加快音量调节响应：空闲输入立即调度，连续输入按剩余节拍合并，最多两次写入后读回真实状态；补充待执行目标反馈和回归测试。
- [x] 加快工作区显示同步：改用 Niri EventStream，实时同步外部切换与各输出的活动工作区；补充事件、重连和动作回归测试。

以上两项的父代理验证记录与限制见 [docs/latency.md](docs/latency.md)。

- [x] 修复 Settings 无法打开、打开过程中 CPU 占用过高的问题：移除表单与隐藏 Flow 的尺寸反馈循环；新增 `settingsLayoutSettles` 离屏回归检查。
