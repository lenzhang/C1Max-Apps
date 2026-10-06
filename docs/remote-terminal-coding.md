# 远程 Terminal Coding 方案

这个项目的目标是让 C1Max 做一个可靠的输入、显示和网络终端，把 Coding Agent 的运行时放在家里的常驻 Linux 主机上。C1Max 的 MIPS CPU、内存和电池只承担 SSH、终端渲染、中文输入和语音输入，不在设备上运行 Node.js、Rust Agent 或 Herdr。

## 已确定的架构

```text
C1Max Terminal
  └─ Dropbear SSH client ──> home-dev-vm / 其他 Linux 主机
                              └─ Herdr server
                                  └─ Codex / Claude / Pi / 其他 CLI Agent
```

Herdr（[herdrdev/herdr](https://github.com/herdrdev/herdr)）提供服务端持久 PTY、detach/reattach、工作区、窗口和 Agent 状态识别。它是一份 Linux/macOS 单体二进制，适合放在服务器；C1Max 是 MIPS 架构，且设备端不需要这些编排能力，因此不把 Herdr 打进设备镜像。

在当前家庭环境中，选择 NAS 上常驻的 Linux 开发 VM 作为第一部署点。它已经承担开发和 Coding Agent 任务，适合用 systemd 管理；NAS 本体继续只承担基础设施和存储。Herdr 服务端的实际部署参数保留在主机用户目录，不写入这个公开项目。

## 设备端用法

设备 Terminal 的 `agent` 命令只负责建立带 PTY 的 SSH 连接：

```text
agent user@server
agent user@server project-name
agent configure user@server project-name
agent
```

目标主机需要先安装 Herdr 并让 `herdr server` 由 systemd 或其他正式服务管理。第一次进入时，Herdr 会创建或接入指定名称的持久会话。按 `Ctrl-B` 后按 `Q` 分离，服务端 Agent 继续运行；重新执行 `agent` 即可接回。直接输入 `exit` 会关闭当前 SSH 客户端，不会杀掉已经由 Herdr 服务端持有的 PTY。

如果不使用 Herdr，仍可直接使用 `ssh -t user@server`；`agent` 只是把常用的持久 Coding 工作流固定下来。

## 电源策略

设备端新增轻量 `c1max-power-guard`，由 launcher 启动。它通过 vendor `PowerManager` 的 `/dev/socket/PowerLock` 建立 per-client `suslock`：有入站 SSH 会话、Terminal 启动了 `dbclient/ssh/scp`，或者设备接入外部电源且 Dropbear 正在监听时持有锁；SSH 断开且设备改用电池后立即释放。协议帧是 NUL 结尾的 `Register suslock <pid>`，服务端必须返回 `ok` 才算成功，守护每 5 秒重新确认一次。这样插电时可以从远端随时建立 SSH，电池模式仍允许系统深度休眠，电源键可以正常唤醒。

屏幕熄灭计时仍由设置页控制，默认不强制“永不熄屏”。建议 Coding 场景选择 5–10 分钟：屏幕可以省电，活动 SSH 由 suspend lock 保持网络和会话；没有活动连接时设备按系统策略休眠。

## SSH 恢复策略

首次启动 Dropbear 且没有公钥或自定义密码时，设备会自动生成默认密码 `c1max`，以便 UI 或 ADB 不可用时仍能恢复。密码只以 SHA-crypt 哈希存储；第一次登录后应通过设置页或 `sshd password` 改成用户自己的密码。公钥认证仍然优先，已有公钥时不会额外生成默认密码。

## 实施状态和验收

- [x] 在常驻 Linux 主机安装 Herdr，并由 systemd 用户服务管理；启用 linger，重启后自动恢复。
- [x] C1Max 增加 `agent` SSH attach 命令和默认目标配置。
- [x] C1Max Dropbear 增加可恢复的默认密码路径。
- [x] C1Max 增加按 SSH 活动和供电状态管理的 PowerManager suspend lock。
- [x] 设备唤醒后重新部署镜像，验证 `sshd status`、`agent configure`，并确认 PowerManager 重启后守护锁仍然有效。
- [x] 真机空闲验证：外部供电时连续 5 分钟无 SSH 会话，USB/ADB、Ping、2222 端口均保持可达；PowerLock ACK 持续有效，`c1max-power-guard` RSS 约 608 KB。
- [ ] 继续测量长时间 SSH 会话下的电池消耗和 Wi-Fi 恢复时间，再决定是否把 5 分钟写成新设备的推荐默认值。

项目不把任何私有主机地址、模型密钥、SSH 私钥或服务密码编译进设备包；服务器目标由用户在设备上通过 `agent configure` 自行设置。
