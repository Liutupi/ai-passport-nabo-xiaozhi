# v1.1.0 · 节奏回声 ECHO CLUB 与菜单重设计

## 新增

- **节奏回声 ECHO CLUB**（工具菜单第三项，或语音“打开节奏回声”）：先听一小节示范，下一小节在同样的位置拍回去。回声（单键）、对唱（OK 低音 / 下键高音）、混合三种玩法各 I–IV 四关，88–120 BPM。PERFECT / GOOD / MISS 判定、海报风界面、合成音效，各关最好评级保存在设备上。
- **菜单重设计**：图标、选中高亮条、右对齐数值、滚动条；电台“正在播放”卡片；状态页两列显示完整 IP / 网络名；电池电量条。
- **内存**：菜单面板改为按需创建，NABO 待机空闲内存约 30 KB → 36 KB。

## 下载与安装

- `xiaozhi.bin`：应用镜像，偏移 `0x10000`。
- `ai-passport-nabo-xiaozhi-v1.1.0.zip`：四个镜像、manifest、刷写工具、文档、第三方源码与许可证。bootloader、分区表和资源与 v1.0.0 完全相同。
- `manifest.json`、`SHA256SUMS.txt`：校验和。

**应用 SHA256**：`5c0d5117bc69ee0fd7115e0a73ce2c12df6e409ccd3a6e08ebe134ef39394620`

已安装 v1.0.0：使用 `python tools/flash_release.py --package . --mode upgrade --port 端口 --expected-mac 设备MAC`，只写应用，保留配网、激活和分数。首次安装使用 `install`。详见 [刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.1.0/docs/FLASHING.md)。

验证范围见 [验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.1.0/docs/VALIDATION.md)：界面与全流程已在实机截屏验证；节奏判定手感尚未用实体按键校准，若感觉偏早/偏晚请反馈。

节奏回声的玩法受 AI Passport 社区《节奏闯关 · COPYCAT CLUB》启发，界面、关卡与音色为本项目原创实现。
