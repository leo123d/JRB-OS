# FLASH.md — JRB OS 刷写说明

> **JRB OS (Just Read Book)** — 小纸 Read Pico 纯阅读固件
> 固件位置：`F:\paperread\release\`
> 对应代码提交：`2bdd6dd`（GitHub: https://github.com/leo123d/JRB-OS）
> 生成时间：2026-10-06

---

## 0. 这一版做了什么

**体积**
```
上游 CrossMux readpico   6,307,184 B   余量  241 KB   ❌ 低于 512 KiB 门槛
JRB OS (本版)             4,211,216 B   余量 2,287 KB  ✅
                         −2,095,968 B  (−2.05 MB, −33.2%)
```

**已删除**
| 类别 | 内容 |
|---|---|
| 游戏 | 2048、五子棋、中国象棋、扫雷、推箱子、数独、木鱼、像素开关、计算器 |
| 应用 | AirPage 相册、丑头像、Buddy、待机屏、阅读统计页 |
| 微信读书 | 阅读页进度同步、书架提升、初始进度、`lib/WeReadWebApi` 整个库 |
| 界面语言 | 33 种 → **仅中文 + 英文** |
| 网络栈 | WiFi、Web 服务器、OPDS/Catalog、Calibre、WebDAV、插件系统、USB 挂载 |
| 联网 OTA | 改为 **SD 卡本地刷写**（`FirmwareFlasher` + `SdFirmwareUpdateActivity`） |
| KOReader 同步 | 需联网，一并移除 |
| 失效设置项 | WiFi/OPDS/插件/KOReader/词典下载/时钟/应用可见性（菜单里的空壳行） |

**字库改进（本轮重点）**
- 内嵌 CJK 字体 12/14/16pt 从**未压缩**改为 **DEFLATE 压缩**，**字符覆盖完全不变**
- 单这一项省 **471,072 B**
- 原始未压缩版本备份在 `lib/EpdFont/builtinFonts/raw_backup/`

**保留**
- ✅ EPUB 阅读引擎
- ✅ **EPUB 内容保护解密**（本地 DRM，不依赖网络 —— wolfSSL 因此保留）
- ✅ SD 卡本地 OTA 刷写
- ✅ 本地字库（`.cpfont`）、字典、书签
- ✅ 中文显示（内嵌 3500 常用字 @12pt；大字号正文仍走 SD 卡字库）

---

## ⚠️ 0.5 本版最需要验证的风险（必读）

**内嵌 CJK 字体改压缩了，而这是上游故意没做的事。**

`lib/EpdFont/scripts/build-cn-builtin-fonts.sh` 原文：

> 6 个 CJK 字体同时加载时每个 group 需约 50 KB scratch，会碎片化堆，
> 启动时让 `FontDecompressor` 抛 `std::bad_alloc` **崩溃**。

这是真机现场崩溃（`FontDecompressor.h` 记录了那次 `abort()`，崩溃时仅约 11 KB 空闲堆）。

**我为什么仍这么做**：当前所有解压缓冲都走 `fiFontMalloc`，而它 **PSRAM 优先**（8 MB），
内堆仅作 fallback（`FontAlloc.c:11`）。所以那 50 KB scratch 不再抢那 30–45 KB SRAM。
**但这是读代码得出的推论，未经真机证实。**

### 因此刷完请重点做这几件事

| # | 操作 | 看什么 |
|---|---|---|
| 1 | 开机 | 是否卡在启动 / 反复重启 |
| 2 | **打开一本中文书，连续翻 200 页** | 是否中途崩溃（内存碎片化） |
| 3 | **反复切换字号**（12↔14↔16↔20） | 是否分配失败 |
| 4 | 反复进出阅读页 20 次 | 堆是否恢复 |
| 5 | **串口日志** | 有无 `bad_alloc` / `abort` / `FDC` 错误 / `Failed to allocate` |

**如果出现崩溃，立刻回退**：`lib/EpdFont/builtinFonts/raw_backup/` 里有原始未压缩字体，
或直接 `git revert 2bdd6dd`。

> `FontDecompressor` 内建了 `peakTempBytes` 计数器（`.h` 第 42 行），
> 若日志里能看到它，把它记下来 —— 那是压缩峰值内存的直接证据。

---

## 1. 发布包与校验和

目录：`F:\paperread\release\`

| 文件 | 大小 (B) | SHA256 |
|---|---|---|
| `firmware.bin` | 4,211,216 | `84CDFB448D129F2C…` |
| `firmware.factory.bin` | 4,276,752 | `E48C1E198A3D1786…` |
| `bootloader.bin` | 18,720 | `15F88AE11793EDDC…` |
| `partitions.bin` | 3,072 | `BD0F7954ACA2EF7D…` |
| `boot_app0.bin` | 8,192 | `F94C5D786A7A8FAB…` |

**回退固件**：`F:\paperread\baseline-firmware.bin`（6,307,184 B）
= 未改造的上游 CrossMux readpico。分区表与 JRB OS 相同，所以只写 `0x10000` 即可回退。

---

## 2. 分区表（决定刷写方式）

双槽 A/B，与板厂出厂分区表**不同**：

```
nvs       data  nvs      0x9000    0x5000
otadata   data  ota      0xe000    0x2000
app0      app   ota_0    0x10000   0x640000   ← 6.25 MiB
app1      app   ota_1    0x650000  0x640000   ← 6.25 MiB
spiffs    data  spiffs   0xc90000  0x360000
coredump  data  coredump 0xFF0000  0x10000
```

**从板厂固件或其他系统首次刷入，必须整表刷写**（四件套）。只写 app 会因偏移不同而失败。

---

## 3. 刷写前：完整备份（**必做**）

设备置入下载模式（按住电源键插入 USB），然后：

```powershell
$esp = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

# 查看串口
[System.IO.Ports.SerialPort]::GetPortNames()

# 完整 16 MB 备份（COMx 换成实际串口）
& $py $esp --chip esp32s3 --port COMx --baud 921600 read_flash 0 0x1000000 "F:\paperread\backup-full-16mb.bin"
```

**校验大小必须是 16,777,216 B。**

---

## 4. 整表刷写

```powershell
$esp = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$py  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$rel = "F:\paperread\release"

& $py $esp --chip esp32s3 --port COMx --baud 921600 write_flash `
  0x0      "$rel\bootloader.bin" `
  0x8000   "$rel\partitions.bin" `
  0xe000   "$rel\boot_app0.bin" `
  0x10000  "$rel\firmware.bin"
```

刷完**拔掉 USB 再重新开机**。

---

## 5. 首次开机验收清单

| # | 检查项 | 预期 |
|---|---|---|
| 1 | 开机 | 正常启动，无反复重启 |
| 2 | **语言列表** | 只有中文 + 英文（不再有 33 种） |
| 3 | **首页「应用」入口** | **完全消失** |
| 4 | **微信读书入口** | **完全消失** |
| 5 | **设置里的 WiFi / OPDS / 插件 / 联网更新** | **全部消失** |
| 6 | 设置里的「SD 卡固件更新」 | **应保留**（这是唯一的 OTA 途径） |
| 7 | 书库 | 能扫到 SD 卡书籍 |
| 8 | **中文显示** | 内嵌 3500 常用字可直接显示；**大字号正文仍需 SD 卡字库**（`fonts/` 里原有 `.cpfont`） |
| 9 | 阅读页 | 正常翻页、目录、书签 |
| 10 | **打开一本带 DRM 的 EPUB** | 应仍能解密阅读（验证 wolfSSL 未被误删） |
| 11 | 睡眠/唤醒 | ≥3 次成功，无 panic |
| 12 | 串口日志 | 40 秒内**无 panic / 无 OOM** |

**已知会保留的问题**（阶段 3 修）：
- 中文**文件名**乱码（`寻迹.epub` 显示为 `瀵昏抗`）
- CSS 因内存不足被静默跳过
- 横屏页脚裁切
- 首页 Tab 未收敛，部分 Tab 点击可能空页

---

## 6. 出问题怎么办

| 症状 | 处理 |
|---|---|
| 反复重启 / 起不来 | 刷回 `baseline-firmware.bin` 到 `0x10000` |
| 认不到串口 | 换 USB 口/线；确认进下载模式 |
| 完全刷不动 | 刷回 §3 的完整备份：`write_flash 0x0 backup-full-16mb.bin` |
| 生僻字显示方框 | 内嵌字库只覆盖 3500 常用字；确认 SD 卡 `fonts/` 有你原有的 `.cpfont` |
| 书籍打不开（DRM） | 报告给我，需检查 wolfSSL 链接 |

---

## 7. 重新构建

```powershell
$env:PYTHONIOENCODING = "utf-8"
Set-Location F:\paperread\crossmux

# 一次性平台补丁（漏了必然失败，见 README「前置」）
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts\patch_pioarduino_cache.py --prepare-platform

git checkout 2bdd6dd
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e readpico
```

冷编译约 5 分钟，增量约 1 分钟。
