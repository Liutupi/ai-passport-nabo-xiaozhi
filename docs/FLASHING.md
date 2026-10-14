# 刷写与恢复

## 镜像与分区

仅支持 ESP32-C3、8 MB Flash 的 AI Passport。串口可能是 COM8，也可能是其他名称；必须使用自己的端口和 MAC。

| 内容 | 偏移 | 分区上限 |
|---|---|---|
| bootloader.bin | `0x0` | `0x8000` |
| partition-table.bin | `0x8000` | `0x1000` |
| NVS（不附带、不覆盖） | `0x9000` | `0x6000` |
| PHY（不附带、不覆盖） | `0xf000` | `0x1000` |
| xiaozhi.bin | `0x10000` | `0x3f0000` |
| assets.bin | `0x400000` | `0x300000` |
| 原 files（不附带、不覆盖） | `0x700000` | `0x100000` |

Release ZIP 是独立镜像集合，不是可直接写到 `0x0` 的单个文件。不要使用 `erase-flash`，不要把已有设备的 8 MB 整片备份传播给别人。

## 准备

下载并解压 `ai-passport-nabo-xiaozhi-v1.0.0.zip`。在解压目录执行：

```powershell
python -m pip install -r requirements-flash.txt
python -m esptool --chip esp32c3 --port COM8 chip-id
python tools/flash_release.py --package . --mode inspect
```

第一条 esptool 命令用于读取设备 MAC；将后续命令中的 `YOUR_DEVICE_MAC` 替换为它。关闭串口监视器再刷写。工具默认只检查包，只有明确指定 upgrade/install 和设备身份才会写入。

## 已安装本定制版：应用升级

```powershell
python tools/flash_release.py --package . --mode upgrade --port COM8 --expected-mac YOUR_DEVICE_MAC
```

工具核对包内 SHA256、ESP32-C3、8 MB、未启用安全启动/Flash 加密，备份并校验整片 Flash，再比对当前 bootloader、分区表和资源。匹配时只写 `xiaozhi.bin`，写后校验应用和全部未修改区域，再重启。配网、激活、最高分等 NVS 数据保留。

若分区/资源不同，工具会在写入前停止。先阅读失败原因，不要绕过检查继续用裸 esptool 写入。

## 原始工牌或首次安装

```powershell
python tools/flash_release.py --package . --mode install --port COM8 --expected-mac YOUR_DEVICE_MAC
```

安装模式会写 bootloader、分区表、应用和资源，仍保留 NVS、PHY 和末尾 files 区。工具会先生成本机私有的整片备份。NVS 字节保留不表示旧 Muse/其他固件的账号能迁移到小智；首次使用小智仍按屏幕配网、激活。

首版四镜像安装来自此前已验证的分区和资源组合；最终应用已实机验证。发布时没有对另一块空白设备重新执行全套首次安装，适配范围与验证边界见 [VALIDATION.md](VALIDATION.md)。

## 备份与恢复

工具把备份、过程日志和校验记录保存到当前目录的 `private/`；该目录可能含凭据，只能留在本机。恢复应使用**同一块设备自己的备份**，先读取 MAC、检查备份 SHA256 和 Flash 大小，再按备份时记录的布局恢复。

刷写只处理普通未加密设备，不修改 eFuse。自定义分区没有 OTA 槽，后续继续使用本项目的 USB 刷写包。
