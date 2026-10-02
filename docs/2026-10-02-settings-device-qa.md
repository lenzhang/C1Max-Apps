# Settings 0.2.0 真机验证

代码：PR [#2](https://github.com/zhuzhe1983/C1Max-Apps/pull/2)，合并提交 `0b97894`。设备为 C1 Max，MIPS Linux 4.4.94，800×340。

## 安装

从设备当前版本复制独立发布目录，加入 Settings 可执行文件、图标、菜单和 manifest，并更新亮度恢复所需的 `c1max-volume`。逐文件校验后原子切换 `current`；保留其他应用、私人应用、全部用户数据和上一版本。

本次设备目录：`/storage/apps/releases/20261002-settings-0b97894`。

## 已通过

- 7 个分类分别执行 `--section N --smoke-ms 3500`，退出码均为 0，运行中捕获并检查实际 framebuffer。WLAN、显示与熄屏、声音、USB、无线调试、电池、关于本机布局正常。
- 使用生产 Wi-Fi 事务函数进行真机连接测试：创建临时不可达网络后取消，恢复原连接、已保存网络数量和各网络启用/停用状态。
- 对当前已保存网络通过临时配置尝试错误密码，实际收到 `TEMP-DISABLED`，失败后自动连回原网络；原来的密码条目保留。
- 上述测试前后 `wpa_supplicant.conf` 的 SHA-256 完全一致，没有调用成功保存路径，也没有留下临时配置。
- 随设备维护完整重启后，Settings 安装仍在，Wi-Fi 和 root ADB 自动恢复。测试期间临时防休眠设置已恢复。

## 范围

这次没有实际切换 USB 功能或开启无线 ADB，也没有播放音频。只读页面 smoke 不代表每个系统选项均已进行交互测试。Wi-Fi 成功保存、保存失败等分支另由仓库中的 10 项事务回归覆盖。

原厂升级保护属于主仓库 `root-implant/update-guard`，不包含在 Apps 发布包中。
