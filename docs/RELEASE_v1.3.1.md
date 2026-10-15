# v1.3.1 · 微信读书左右翻页

滑动后进入下一页时，现在可直接用工牌翻页：**双击上键上一页、短按 OK 下一页**，保留上下滑动、按住连续滑动和长按 OK 退出遥控。

## 更新

- 上键单击、双击与按住分开处理，双击上一页不会先触发一次滚动；按住上键 450 ms 后开始连续滑动。
- “鼠标与翻页”新增上一页和向右翻页，电脑操作共 27 项，旧快捷键编号不变。
- 逐系统迁移旧版自定义映射，保留 Windows / Mac 选择和滚动方向。
- HID 描述符与 v1.3.0 相同，从 v1.3.0 升级无需因本次改动重新配对；从 v1.2.0 及更早版本升级仍需在电脑删除旧配对后重新配对，以启用滚轮鼠标。

## 下载与升级

- `xiaozhi.bin`：4,006,288 字节的应用镜像，偏移 `0x10000`。
- `ai-passport-nabo-xiaozhi-v1.3.1.zip`：四个镜像、manifest、刷写工具、使用/编译/交接文档、第三方源码与字体许可证。
- `manifest.json`、`SHA256SUMS.txt`：镜像来源、偏移与下载文件校验和。

**应用 SHA256**：`206a824014e1b04b4e02b64cad2f462d8610a620dd721fe3596ca8d59a701fbf`

已安装本定制版：解压 ZIP，按[刷写说明](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.3.1/docs/FLASHING.md)使用 `tools/flash_release.py --mode upgrade`，核对自己的端口和设备 MAC。工具备份并校验后只写应用，保留配网、激活、配对和用户记录。bootloader、分区表与资源沿用 v1.0.0–v1.3.0 的同一份镜像。此单应用分区不使用标准 OTA。

## 验证与交接

发行应用与已刷入并获得用户验收的文件完全相同，未用重新编译的文件替换。140 项相关宿主回归、12 项生产手势测试通过；整片备份、应用及保留区写后校验、启动联网、设置与最高分保留、LCD 显示检查通过。用户在微信读书上确认上一页、下一页、上下滑动及退出均正常。

实机验收仅覆盖当前工牌与电脑，未重新测试其他操作系统/阅读软件及未改动的音频、游戏、连续对话。具体范围见[验证记录](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.3.1/docs/VALIDATION.md)，维护入口见[交接文档](https://github.com/Liutupi/ai-passport-nabo-xiaozhi/blob/v1.3.1/docs/HANDOFF.md)。源码提交、配置与镜像摘要另见 `validation/v1.3.1.json`。素材和第三方组件仍按各自原始许可证提供。
