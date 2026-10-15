# 维护交接

## 当前交接状态

最新 Release 为 **v1.1.0**，应用 3,945,968 字节，SHA256 `5c0d5117bc69ee0fd7115e0a73ce2c12df6e409ccd3a6e08ebe134ef39394620`。新增节奏回声、菜单重设计和菜单面板按需分配；v1.1.0 的验证范围见 [VALIDATION.md](VALIDATION.md) 首节。发行包的 bootloader、分区表和资源镜像直接复用 v1.0.0 Release（与实机逐字节一致），只替换应用：本机重新构建的 bootloader/资源与已部署字节不同，若把它们打进包，`flash_release.py --mode upgrade` 会对现有设备拒绝写入。

下面是 v1.0.0 的原始交接状态。

首个定制 Release 为 **v1.0.0**，应用 3,901,088 字节，SHA256：

```text
b18f915d0295181609b225b475511e1e8a5e60d950b2390fc33191e7c0278d40
```

这是已刷入并获得“摇骰子、木鱼、网络电台三个都能打开”反馈的镜像。此前连续对话内存修正版获得“连续对话正常”；连续蓄力和音量获得“连跳和音量都合适”。各项证据对应的镜像与限制见 [VALIDATION.md](VALIDATION.md)，不能把此前全部测试说成对最新镜像又完整执行了一遍。

## 代码职责

目录前缀 `xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/`：

| 文件 | 维护职责 |
|---|---|
| ai_passport_board.cc / config.h | 硬件初始化、ADC 按键、双击/长按、语音回调；引脚保持原工牌布局 |
| nabo_display.cc / muse_pixel.c | 224×224 NABO 动画和流式行解码，避免整帧内存分配 |
| nabo_apps.cc | 工具菜单、页面状态、导航、任务/协议所有权、音频切换、设备侧 MCP |
| nabo_voice_commands.c | 有界无堆分配的明确语音命令解析；拒绝否定、询问、多程序歧义 |
| nabo_jump_model.c / nabo_jump_rig.c | 游戏物理、平台和人体动作；保留落地/镜头期间的输入 |
| nabo_jump_sound.c / jump_sounds.bin | PCM 音效、蓄力渐变与切换过渡 |
| nabo_radio*.c / nabo_helix_buffers.c | 电台目录、HTTP 流、解码/分块重采样、碎片堆友好分配 |
| nabo_keys*.c / nabo_pc.c | BLE HID、分组映射与退出回收 |
| nabo_dice*.c / nabo_muyu*.c | 骰子/木鱼模型、画面、声音与持久化 |
| nabo_rhythm_model.c / nabo_rhythm_sound.c | 节奏回声关卡、判定（16 kHz 采样时钟）、合成音色；宿主测试 test_rhythm.py |
| nabo_rhythm_play.c | 音频 worker 与 LVGL 之间的歌曲时钟锚点、音效队列、延迟补偿 |
| nabo_rhythm_ui.c / nabo_rhythm_stage.c | 选关/结算与演奏画面；对象只在程序打开时存在 |
| nabo_*_font_*.c | 由 tools/generate_fonts.py 生成；新增中文界面文字后重新生成，字形不足会显示缺字 |

核心层只通过 `Board::HandleRecognizedSpeech()` 可选接口调用板型，不能在 `application.cc` 中硬编码 NABO 程序关键词。STT 回调通过 `Application::Schedule()` 在主任务显示文字并处理打开指令。其他板型默认不处理该接口。

## 不要退回的修复

**小智内存：** C3 没有 PSRAM。旧 ES8311 整帧双声道临时 vector 和过长队列造成 `std::bad_alloc`，包括长回答后的启动提示循环崩溃。现在用 `stereo_frames.h` 每次 160 帧、640 字节栈缓冲转换；NABO 的 PCM 队列 1 帧，压缩收发缓冲 600 ms。保持 Opus **24 KiB 栈**：真实语音曾使用近 20 KiB，不能根据待机栈水位随意缩小。其他板型的队列默认不变。

**程序切换：** 本地程序持有音频时停止小智，返回前完成本地音频退出；主任务回收已完成的协议初始化任务后再恢复 AudioService，不能叠加约 8 KiB 的已完成任务栈。避免在 LVGL 显示锁内阻塞等待音频任务。

**BLE 退出：** 先断开、停止 host、清空 GATT 属性，再 NimBLE deinit/释放内存。漏掉 GATT 清理会导致 ATT 运行时内存池断言。需使用真实已配对电脑验证退出，空连接测试不足。

**游戏输入与画面：** 落地、镜头移动甚至飞行末段可缓冲下一次蓄力；保留第一次按下时间和提前松开的时长。不能等画面停止才接受按键。失败动作在平台旁结束，不让 NABO 整体飞出屏幕。已把绘制范围缩到有效扫描行和角色/阴影局部区域；避免恢复逐像素全场景检查。

**声音：** 骰子/木鱼继续 16 kHz 双声道 I2S、4×160 帧 DMA。小智 24 kHz 与本地音效的所有权和采样率不同，要先完成退出再切换。蓄力 gain 0.65、15 ms 淡入淡出、约 8 ms 声音切换过渡已被接受。界面 100% 需保留扬声器余量，不能直接恢复满幅。

**按键：** 右侧上键双击回上一级，单击原功能、长按直接回小智。页面父级要保留，特别是设置编辑、状态页、电台目录、电脑分组。左侧电源键不是可读取的 MCU 按键。

**菜单内存：** LVGL 使用系统堆（`CONFIG_LV_USE_CLIB_MALLOC`）。菜单面板在进入菜单页时创建、回到 NABO 或进入游戏时删除，节奏回声同理。不要改回开机常驻：v1.1.0 首次常驻版本在开机 MQTT 发布工具列表时出现 `esp-aes: Failed to allocate memory`，改为按需后待机空闲堆约 30 KB → 36 KB。

**节奏判定：** 判定以 OK/下键的 press-down 时间戳为准（下键按下由板型 `OnPressDown` 转发），通过 worker 写 I2S 前记录的锚点换算到歌曲采样位置。默认补偿 42 ms，可用 `>rhythm.latency=` 调整；`>rhythm.best.reset` 清除成绩，脚本测试后应执行，避免留下假成绩。

## 诊断与回归

开发固件保留串口命令，如 `>apps.status`、`>apps.open=home`、`>apps.open=jump`。它们用于定位页面和资源状态；使用实体按键/真实语音做最终验收。游戏诊断必须打开测试抑制模式，避免自动操作刷新用户最高分。

主页 `VoiceMemory` 每约 5 秒打印 heap、largest、Opus 栈余量和音频计数，只在主页工作，不干扰游戏绘制。记录长回答、打断、返回主页后再对话，检查 abort/assert、栈和最大连续内存块。日志可能包含 ASR/TTS、网络信息，只保存到被 Git 忽略的 `private/`，公开问题仅用脱敏堆栈和统计。

按影响运行测试：语音解析/PCM/游戏/电台生产 C 测试，以及上游宿主测试。修改音频/生命周期必须加真实连续对话与程序来回切换；修改 BLE 需已配对退出；修改导航需验证所有父页面。不要反复运行不相关测试替代硬件验证。

## 下一版发布流程

1. 从本仓库的 `v1.0.0` 标签开始，保持板型身份 `ai-passport-nabo` 和分区接口。
2. 读取源码中的 AGENTS.md，修改最窄职责层，保留第三方许可。改动编译配置时更新锁定/配置并记录理由。
3. 宿主测试、目标构建、相应实机回归；给每份设备验证摘要记录具体应用 SHA256。
4. 打包四个**构建输出镜像**，从不打包已配网设备的整片 Flash、NVS 或 private。应用升级仍只写应用分区。
5. 更新 CHANGELOG、交接、验证边界、manifest 和 SHA256SUMS；标签与源码提交绑定。
6. 创建 Release 后下载公开资产重新核对 SHA256 和 ZIP 成员，确认文档/许可证随包。

## 已知边界

自定义单应用分区不能使用标准 OTA。网络语音需要小智服务，电台 URL 也依赖第三方网络。仅此 ESP32-C3 工牌经实机验证，其他上游板型保留源码但不是本项目的兼容承诺。首版不保证任意网络环境永久无重连，也不承诺全阶段 60 FPS；用户实际接受的连跳和声音、测量范围均在验证记录中。
