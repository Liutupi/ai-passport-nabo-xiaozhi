# v1.0.0 · AI Passport NABO 小智首版固件

面向 FoloToy AI Passport（ESP32-C3 / 8 MB）的完整定制版：NABO 对话动画、人体动作跳一跳、摇骰子、功德木鱼、47 台网络电台、BLE 电脑快捷键和设备设置。

这份应用已刷入实机，用户确认摇骰子、木鱼、网络电台均可通过小智语音打开。包含连续对话内存修复、连续蓄力、高音量余量和右侧上键双击返回。

## 下载

- `ai-passport-nabo-xiaozhi-v1.0.0.zip`：四个独立镜像、manifest、刷写工具、使用/编译/交接文档和 Helix 对应源码/许可证。
- `xiaozhi.bin`：应用镜像，只适用于已有本定制分区和资源的设备，偏移 `0x10000`。
- `manifest.json`：四个镜像的偏移、大小及 SHA256。
- `SHA256SUMS.txt`：ZIP、应用、manifest 的下载校验和。

**应用 SHA256**：`b18f915d0295181609b225b475511e1e8a5e60d950b2390fc33191e7c0278d40`。

本项目发行版本为 v1.0.0；继承的小智应用版本字符串是 2.5.0。

## 安装

已安装本定制版使用 `upgrade`，只更新应用；首次安装使用四镜像 ZIP 的 `install`。工具要求指定设备 MAC、先备份、检查分区并完成写后校验。详情见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.0.0/docs/FLASHING.md)。不提供 NVS 或整片设备备份，不执行整片擦除；自定义单应用布局不支持官方标准 OTA 镜像。

真实语音打开在这份镜像验证；7 轮连续对话、26 项导航和帧间隔统计来自此前对应修正版，范围分别记录在 [验证说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.0.0/docs/VALIDATION.md)。发布时未在第二块空白设备重做首次安装。

使用和后续维护从 [README](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/tree/v1.0.0) 与 [交接文档](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.0.0/docs/HANDOFF.md) 开始。
