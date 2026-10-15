# v1.2.0 · 海报风界面与清晰字体

## 更新

- **海报风菜单**：工具、设置、状态、电台、电脑快捷键全部换成节奏回声同款风格：米白网格底、粗体英文大标题、实心选中色块加错位阴影、行号，每页一种强调色。
- **更清晰的中文**：界面字体换成思源黑体 4-bit 抗锯齿，菜单文字放大到 18 px；摇骰子、木鱼、跳一跳、节奏回声的小字同步更新。
- 电池页大号百分比与电量条；电台“正在播放”卡片。
- 保留 v1.1.0 的节奏回声 ECHO CLUB 与菜单按需分配内存。

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `ai-passport-nabo-xiaozhi-v1.2.0.zip`：四个镜像、manifest、刷写工具、文档、第三方源码与字体许可证。bootloader、分区表和资源与 v1.0.0 / v1.1.0 相同。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`2c05a52efc76a38597c0a632082c913f8af0acaf6d357f889a58b92829a3c984`

已安装 v1.0.0 或 v1.1.0：`python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`，只写应用，保留配网、激活、分数和节奏成绩。详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.2.0/docs/FLASHING.md)，验证范围见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.2.0/docs/VALIDATION.md)。

字体：思源黑体 Source Han Sans（Adobe，OFL 1.1）、Archivo Black、Space Mono（OFL 1.1），许可证随包提供。
