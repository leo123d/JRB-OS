# JRB OS

**Just Read Book** — 一个只为「读书」而存在的墨水屏固件。

运行在 **小纸 Read Pico**（MindReset RDP-G01-W，ESP32-S3-N16R8，4.7″ 684×1216 16 级灰度墨水屏）上。

> ## 🔌 [**网页刷机工具 →**](https://leo123d.github.io/JRB-OS/)
> 用 Edge / Chrome 直接刷入，**不需要安装任何软件**。固件在本地通过 Web Serial 写入，不会上传到任何服务器。
>
> 首次刷入必须**整表刷写**（bootloader + partitions + boot_app0 + firmware），
> 因为本固件使用双槽 A/B 分区表，与板厂出厂分区表不同。页面里有一键完整备份。

> 📖 **接手开发请先读 [`docs/jrbos/HANDOVER.md`](docs/jrbos/HANDOVER.md)** —— 交接文档，
> 含构建环境关键补丁、已完成的实测数据、六条硬教训、下一步优先级。
>
> 其它项目文档：[字库策略](docs/jrbos/FONT-STRATEGY.md) ·
> [构建踩坑](docs/jrbos/BUILD.md) · [刷写说明](docs/jrbos/FLASH.md) ·
> [阶段1记录](docs/jrbos/PHASE1-LOG.md) · [设计文档](docs/jrbos/PAPERREAD-DESIGN.md)

---

## 这是什么

JRB OS 把一台通用墨水屏设备**削成一台纯粹的书**。

没有游戏、没有应用商店、没有微信读书、没有 WiFi、没有浏览器、没有统计排行、没有时钟。
开机就是书，翻页就是全部。

### 目标

| 项 | 目标 | 当前 |
|---|---|---|
| 固件体积 | ≤ 3,349,499 B（腾出空间内嵌中文字库） | **4,683,504 B** |
| 中文字库 | GB2312 一二级 6763 字，内嵌 20pt / 14pt / 12pt 三档 | 待做（暂用 SD 卡字库） |
| 界面语言 | 中文 + 英文 | ✅ 已达成 |
| 首页入口 | 3 项（最近阅读 / 书库 / 设置） | 进行中 |
| 联网能力 | 完全移除 | ✅ 已达成 |

### 已完成

- ✅ **删除全部游戏** — 2048、五子棋、中国象棋、扫雷、推箱子、数独、木鱼、像素开关、计算器
- ✅ **删除 AirPage 相册、丑头像、Buddy、待机屏**
- ✅ **删除微信读书**（含阅读页的进度同步、书架提升、初始进度）
- ✅ **删除整个网络栈** — WiFi、Web 服务器、OPDS、Calibre、WebDAV、KOReader 同步、插件系统、联网 OTA
- ✅ **界面语言 33 种 → 中文 + 英文**
- ✅ **OTA 改为 SD 卡本地刷写**（不再需要联网）
- ✅ **保留 EPUB 内容保护解密**（本地 DRM，不依赖网络）

**体积进展**

```
上游 CrossMux readpico   6,307,184 B   余量  241 KB  ❌ 低于 512 KiB 门槛
JRB OS                    4,683,504 B   余量 1,869 KB ✅
                         −1,623,680 B  (−1.55 MB, −26%)
```

### 待做

- ⬜ 内嵌 GB2312 中文字库（20pt / 14pt / 12pt，压缩后约 2.68 MB）
- ⬜ 首页收敛为 3 项
- ⬜ 修复中文文件名乱码（`寻迹.epub` → `瀵昏抗`）
- ⬜ 修复 CSS 因内存不足被静默跳过
- ⬜ 修复横屏页脚裁切
- ⬜ 阅读页 UI 重写（零装饰 + 4 键工具栏）
- ⬜ 段落级划线批注
- ⬜ 抬腕唤醒（SC7A20H 加速度计）
- ⬜ 数据目录迁移 `.crosspoint/` → `.jrbos/`

---

## 硬件的真实瓶颈

不是 Flash，是**内存**。

| 资源 | 容量 |
|---|---|
| Flash | 16 MiB（A/B 双槽，每槽 6,553,600 B） |
| PSRAM | 8 MiB（octal） |
| **SRAM** | **512 KB，其中可用内部堆只有 ~30–45 KB** |

全刷一页 684×1216 的 16 级灰度图就是 684×1216÷2 ≈ **416 KB** —— 所以排版、CSS 解析、字库缓存全都要挤在那几十 KB 里。这是本项目所有痛苦的根源。

**已知的连带后果**：CSS 解析器需要 65,536 B 连续内存，拿不到就**静默跳过样式**（待修）。

---

## 构建

### 前置（关键，漏了就必然失败）

```bash
# 一次性：把 tool-scons 从 pioarduino 平台的 COMMON_IDF_PACKAGES 移除，
# 否则平台的 stub 会覆盖 PlatformIO 核心正在使用的 SCons。
python scripts/patch_pioarduino_cache.py --prepare-platform
```

> ⚠️ 上游**只在 CI 里调这个补丁**，本地开发文档里没提。
> 漏了会得到 `ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'`，
> 而且全程编译 0 个文件 —— 报错信息有很强的误导性。

### 编译

```bash
export PYTHONIOENCODING=utf-8      # 上游 i18n 生成会打印阿拉伯语，Windows 默认 cp936 会卡住
pio run -e readpico
```

冷编译约 5 分钟，增量约 1 分钟。

### 刷写

分区表是**双槽 A/B**（`app0`/`app1` 各 6.25 MiB），与板厂出厂分区表不同，
所以**首次刷写必须整表刷写**：

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 write_flash \
  0x0      bootloader.bin \
  0x8000   partitions.bin \
  0xe000   boot_app0.bin \
  0x10000  firmware.bin
```

刷前务必先完整备份 16 MB：

```bash
esptool.py --chip esp32s3 --port /dev/ttyACM0 read_flash 0 0x1000000 backup-full.bin
```

---

## 项目由来与许可

**JRB OS 是 [CrossMux](https://github.com/0x1abin/crossmux) 的衍生作品**，而 CrossMux 本身是
[CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) 的分支。
本项目的价值在于**做减法** —— 我们没写阅读引擎，我们把它周围的一切拆掉。

上游原始文档见 [README.upstream.md](README.upstream.md)。

参考过的其他固件

- [read-pico-reader](https://github.com/wegooo-cell/read-pico-reader) — 同类尝试，但无桌面模拟器、历史极短
- [RickyOS](https://chinoryunqin.github.io/RickyOS-site/) — 闭源，仅作设计参考

**许可**：MIT。原始版权归 Dave Allie 与 FreeInk（见 [LICENSE](LICENSE)）。
本项目的修改同样以 MIT 发布。

---

## 目录结构

```
src/
  activities/        界面活动（home / reader / library / settings / util）
  components/        渲染组件与主题
  ota/               SD 卡本地 OTA（FirmwareFlasher / OtaBootSwitch）
  util/              工具（EPUB 解析、字典、HTML 转换…）
lib/
  Epub/              阅读引擎
  I18n/              界面文案（仅 EN + ZH_CN）
freeink-sdk/         上游硬件抽象层（git submodule）
```

已删除：`src/network/`、`src/activities/network/`、`src/activities/browser/`、
`src/activities/plugins/`、`lib/WeReadWebApi/`、`src/NetworkStartup.*`、
`src/WifiCredentialStore.*`、`src/OpdsServerStore.*`

---

## 参与

目前是个人项目，主要目标是把这台设备变成一台真正好用的纯阅读器。
如果你也在用小纸 Read Pico，欢迎提 Issue 说说什么最影响你读书。
