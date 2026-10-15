# v1.3.0 · 滑动遥控、跳一跳进阶与蓝牙修复

## 更新

- **电脑快捷键只留高频操作**：去掉新建/关闭标签、地址栏等低频项，25 个操作分为“鼠标与翻页 / 常用操作 / 影音播放 / Typeless 与 AI”。
- **滑动遥控**：工牌变成蓝牙滚轮——上键向上滑、下键向下滑（按住连续滑），OK 返回上页，长按 OK 退出。刷网页、文档、短视频都能用；Mac 自动适配自然滚动，可在“连接与设置”里反转方向。新增鼠标单击/右键、显示桌面、快退/快进。
- **跳一跳越往后越难**：间距逐步加大，出现缩小的迷你平台；约第 20 跳起出现只容一人的小平台——外带咖啡、拿铁、冰美式、迷你电脑、小音箱、黑胶唱片，站上去额外加 1 分。
- **修复蓝牙键盘**：进入电脑快捷键时可能内存不足、离开后小智不恢复且主页 NABO 消失（v1.2.0 同样存在），现已修复。
- 应用体积减小 47 KB，分区剩余约 120 KB。

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `ai-passport-nabo-xiaozhi-v1.3.0.zip`：四个镜像、manifest、刷写工具、文档、第三方源码与字体许可证。bootloader、分区表和资源与 v1.0.0–v1.2.0 相同。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`068a543dfbd57e912fbcbd7c7fcb827afafca68230df494f68bb4c336e111e47`

已安装 v1.0.0–v1.2.0：`python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`，只写应用，保留配网、激活、分数和节奏成绩。**升级后请在电脑蓝牙设置中删除 `NABO-Xiaozhi-xxxx` 再重新配对一次**，否则滚轮和鼠标点击无效。详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.3.0/docs/FLASHING.md)，验证范围见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.3.0/docs/VALIDATION.md)。

字体：思源黑体 Source Han Sans（Adobe，OFL 1.1）、Archivo Black、Space Mono（OFL 1.1），许可证随包提供。
