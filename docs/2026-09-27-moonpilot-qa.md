# HID 键鼠 / MoonPilot 拆分验证（2026-09-27）

## 本次实现

- 原 `hidpilot` ID 保留，版本 0.2.0，入口改为「HID 键鼠」。专注触控板、实体键盘、鼠标按钮与修饰键，USB gadget / 清理进程保持原实现。
- 新增 `moonpilot` 0.1.0：Sunshine 地址、PIN 配对、应用列表、Moonlight 串流、手动输入、视觉模型单步／十步动作、ASR／对话／TTS 配置。
- 移除 AI 摄像头路径，模型输入改为完整解码桌面。首次启动迁移旧模型配置，之后使用独立私有目录。
- Moonlight Embedded 固定 `f32e415aea6797d261d6b470dcf8bf18727341c2`，common-c `b126e481a195fdc7152d211def17190e3434bcce`，采用 MbedTLS。
- MoonPilot launcher 图标由内置 imagegen 生成，RGBA 1254×1254，alpha 范围 0–255；原始提示词保存在 `launcher/assets/moonpilot-prompt.json`。

## 已通过

1. 全应用静态 MIPS 构建；新增 MoonPilot 约 2.9 MiB，HID UI 约 1.8 MiB。公共包 18 项，本地额外应用保留，不发布私人配置。
2. 独立 Python TLS / PIN fixture 与 C++ 客户端联测（ASan / UBSan）：双向 PIN 证明、客户端 TLS 证书、服务端证书固定、错误 PIN 和篡改证明拒绝、配对后重连、应用列表、启动参数、拒绝抢占其他应用、取消、文件权限。**Fixture 不是真实 Sunshine。**
3. 桌面帧解析：分片 640×360 帧、800×450 上限、默认视频播放器仍限制 512×288，超限拒绝。StreamPlayer 的视频、HLS、字幕和字幕后台线程回归通过。
4. HID 报告布局／字符映射，迁移后模型动作校验、语音 API 和取消通过。GameStream 虚拟键编码校验包含必需的 `0x8000` 标签。
5. 真机原厂 MPlayer + YUV 适配器静音解码 640×360 H.264：首轮 30 帧文件输出 29 帧；随后以 15 fps 实时向 stdin 喂 90 帧 AUD 测试流，输出 89 帧，首帧 150 ms。输入流末尾额外保持打开 2 秒，证实首帧不依赖 EOF。测试源无 B 帧、使用 ffmpeg testsrc2，不涉及私人视频。
6. 整包 SHA-256 校验后激活 `/storage/apps/releases/20260927-124509-e0524f63`。设备截图确认两个新入口可见、图标透明；测试菜单只有这两项，非正常全量列表。

连续解码测试工具为 `moonpilot/tools/decoder-test.cpp`，诊断程序不进入安装包。150 ms 只代表此测试的本地管道首帧时间，不包含 Sunshine、网络、LVGL 展示或模型推理延迟，也不是最高画质档的性能承诺。

## 待实测和限制

- 用户选择先开发，尚无 Sunshine 主机 IP。真实主机的 PIN 配对、串流、远程键鼠和“画面 → 模型 → 操作 → 新画面”闭环没有通过验收。
- 初次安装后 USB 曾断开；用户唤醒后已完成下方补装与界面验证。恢复日志有系统 resume 记录，没有此次重新开机证据；仅凭这些记录不能确定最初断线的全部原因。
- 之前 HID 功能的完整主机输入证据见 [0.1.0 验证](2026-09-27-hidpilot-qa.md)；本次补验新界面的 USB 开关、ADB 共存与退出恢复，未再次向电脑发送鼠标／文字。
- 本轮未连接实际模型服务。原 LocalAI TTS 的 CUDA/NVML 服务故障没有在此次修复；接口 fixture 成功不代表服务器播报可用。
- 词典仅接收视频，电脑保留声音。只支持手动填写 IP、现代 Sunshine / GameStream 和 H.264；没有发现服务、拖拽或多显示器布局。

私有测试输出位于忽略的 `.build/moonpilot-qa/`；设备临时文件使用 `/storage/apps/data/moonpilot-qa/`，验收结束后清理。临时菜单通过进程环境 `C1L_CONFIG` 指定，未改正常 `launcher/apps.txt`；退出测试 launcher 后常规入口即加载全量菜单。

## 连接恢复后的补验

- 已补装首次启动提示、端口错误提示的最终代码。MoonPilot 主机页和未连接状态的操作页已真机截图；没有把静态页面当作串流验收。
- 第一次打开后，MoonPilot 配置与旧 HIDPilot 私有设置逐字节相同，权限为 0600。随后分别在 URL、模型名字段输入再退格并保存，比较四组服务和播报设置，内容全部未变。旧文件保留，测试没有写入主机地址。
- 用设备真实支持的 event0 字母／Shift 组合、event1 退格／返回事件检查主机字段：IP 输入 `hi`，转焦点到端口并退格，IP 保持不变；返回取消焦点，没有误删。清空测试内容、恢复默认端口 47989。
- MoonPilot 空闲主机页 RSS 3144 kB，HID 已连接界面 RSS 1920 kB；前者没有摄像头或 HID 描述符。该数字不包含未来串流解码子进程和模型请求峰值。
- HID GUI 点连接后，`ffs.hidpilot`、`ffs.adb`、`mtp.gs0` 共存，ADB shell 正常。按电源退出，状态回到 OFF，HID 接口移除，ADB / MTP 恢复，前台返回 launcher。

最终激活 `/storage/apps/releases/20260927-131257-69e5e971`，设备 MoonPilot / HID 可执行文件 SHA-256 与最终本地包一致；退出两项专用测试菜单后，通过正常 `launcher/apps.txt` 重启，恢复 19 个本地入口（公开目录 18 项）。USB 状态 OFF，仅 ADB / MTP；20 分钟及息屏后 15 秒参数保持不变。设备测试目录已清理，证书和私有设置保留。
