# 来源与许可

## XiaoZhi

基于 <https://github.com/78/xiaozhi-esp32> 提交 `4632dc51f0a5ad26e08542e131e6e48da41e4ff3`，上游 MIT 许可证完整保留在根目录及 `xiaozhi-esp32/LICENSE`。新增/修改的项目代码沿用 MIT；下列第三方内容按其各自条款。

## Helix MP3

**Helix DNA Client technology included. Copyright (c) RealNetworks, Inc., 1995-2002. All rights reserved.**

源码位于 `xiaozhi-esp32/main/boards/folotoy/ai-passport-nabo/helix/`。保留 LICENSE、RPSL、RCSL、源文件版权及修改标记；不是 MIT 重新授权。NABO 的 portable C 测试路径说明见该目录 README.NABO.md，生产 ESP32-C3 使用 RISC-V 路径。覆盖源码、原始许可与本通知随源码和固件 ZIP 提供。
板型中的 `nabo_helix_buffers.c` 也是 RPSL/RCSL 标记的 Helix 派生文件，不属于新增 MIT 代码；其原始许可块和修改说明保留，文件随 Release 对应源码一起提供。

## 字体与组件

中文菜单字体基于 LXGW WenKai，SIL Open Font License 1.1 全文位于板型目录 `OFL-LXGW.txt`。节奏回声的海报标题字体 `nabo_poster_font_28/48` 子集自 Archivo Black（Copyright 2017 The Archivo Black Project Authors，OFL 1.1，全文 `OFL-ArchivoBlack.txt`），小号标签字体 `nabo_mono_font_12` 子集自 Space Mono Bold（Copyright 2016 The Space Mono Project Authors，OFL 1.1，全文 `OFL-SpaceMono.txt`）。字体由 `tools/generate_fonts.py` 用 lv_font_conv 1.5.3 生成，来源校验和记录在 `game-art/fonts.json`；源 TTF 不入库。ESP-IDF、LVGL、Espressif/其他 IDF 组件分别保留其上游许可，通过 `main/idf_component.yml` 与 `dependencies.lock` 解析；MIT 不覆盖这些依赖的独立条款。

## NABO 和游戏资源

NABO 美术来自项目拥有者提供的原图与此前 NABO 工牌资源。医院、书房、播客、木鱼、咖啡、靶心平台为本定制项目制作；游戏机制和等距画面参考微信《跳一跳》。没有取得或使用微信原版源码。

蓄力、起跳、正中 PCM 参考项目拥有者指定的“新空气”AI Passport 社区版本，来源 <https://ai-passport.folotoy.cn/plays/778/> 的公开固件；来源/样本校验和见 `game-art/original-sound-reference.json`。落地/跌落及淡化蓄力变体在本地制作。社区页面没有为这些声音提供已确认的独立开源许可，因此**不将它们声明为 MIT 或公有领域**。素材权利与源码许可分开；再分发/替换素材时须保留来源并核实适用条款。

节奏回声（ECHO CLUB）的“先听示范、再拍回去”玩法受 AI Passport 社区《节奏闯关 · COPYCAT CLUB》启发；界面、关卡节奏型、合成音色和全部代码均为本项目原创实现，未使用该作品的源码、图像或声音。

本项目不包含微信客户端、社区整片固件、私人账号数据或他人的设备备份。
