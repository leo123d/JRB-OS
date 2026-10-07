# HANDOVER.md — JRB OS 交接文档

> **项目**：JRB OS（Just Read Book）— 小纸 Read Pico 纯阅读墨水屏固件
> **仓库**：https://github.com/leo123d/JRB-OS （public，账号 `leo123d`）
> **交接时间**：2026-10-06
> **当前提交**：`7dff91d6`（本地与远端一致）
> **固件**：**4,211,216 B**（相对上游 −2,095,968 B / −33.2%）
>
> ⚠️ 本文档只记录**已验证的事实**。未验证的推论都明确标注了。

---

## 0. 30 秒速览

| 项 | 状态 |
|---|---|
| 构建环境 | ✅ 已打通（有一处**必须**的平台补丁，见 §1） |
| 阶段 0 基线 | ✅ 完成 |
| 阶段 1 减法回收 | ✅ 主要目标完成（回收 2.05 MB / 33.2%） |
| 阶段 2 字库 | 🟡 **压缩现有 CJK 字库已完成**（省 460 KB，**待真机验证**）；完整 GB2312 内嵌待做 |
| 阶段 3 缺陷 | 🟡 B1 调查完毕（**判定为不可复现**）；B2/B3 未动 |
| 阶段 4 UI 重写 | ⬜ 未开始 |
| 阶段 5 数据迁移 | ⬜ 未开始 |
| 阶段 6 真机验收 | ⬜ 未开始（**当前最大缺口**） |
| GitHub 推送 | ✅ 已推送，含子模块 fork |

**一句话状态**：固件已能构建、已缩小三分之一、已可刷机，但**从未在真机上跑过** —— 这是当前最大的未知。

---

## 1. ⚠️ 构建环境（最容易踩的坑，必读）

### 1.1 必须的一次性平台补丁（**漏了必然失败**）

```bash
cd <repo>
python scripts/patch_pioarduino_cache.py --prepare-platform
```

**为什么**：它把 `"tool-scons"` 从 pioarduino 平台的 `COMMON_IDF_PACKAGES` 列表移除，
否则**平台的 stub 会覆盖 PlatformIO 核心正在使用的 SCons**。

**漏了的症状**（极其误导）：
```
ModuleNotFoundError: No module named 'SCons.Tool.FortranCommon'
```
**全程编译 0 个文件**。报错点在 `FortranCommon`，但真问题是整个 SCons 包被换成了垃圾。

**为什么没人知道**：上游**只在 CI 里调这个补丁**（`.github/workflows/{ci,hardware-ci,nightly,sync-build}.yml`），
README 和 docs 的本地开发步骤里**完全没提**。

### 1.2 构建命令

```bash
export PYTHONIOENCODING=utf-8      # 必须：上游 i18n 生成会打印阿拉伯语，Windows 默认 cp936 会卡住
pio run -e readpico
```

耗时：**冷编译约 5 分钟**（870 个目标文件），**增量约 1～1.5 分钟**。

### 1.3 本机环境的特殊之处（换机器时注意）

| 项 | 本机情况 | 影响 |
|---|---|---|
| PlatformIO | 装在 `F:\小纸pico\.paperread-tools\Scripts\pio.exe`（venv），**不在 PATH** | 必须用绝对路径 |
| 系统 PlatformIO | `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` | 与上面**共用同一个包缓存**，效果相同 |
| `github.com:443` | **被网络阻断** | **git HTTPS 推送不通**；已配 SSH-over-443（见 §7） |
| `api.github.com:443` | 通 | `gh` CLI 可用 |
| 项目路径 | **必须纯 ASCII** | **FreeType 无法打开非 ASCII 路径下的字体**，字库生成必须在 `F:\paperread\...` 做 |
| 桌面模拟器 | **跑不通** | 缺 `sdl2-config`（Windows 无此工具）且依赖 `crosspoint-simulator` 需从 GitHub 拉取而网络受限 → **UI 迭代只能靠真机** |

> 详细踩坑记录见 `BUILD.md`。

---

## 2. 仓库与代码状态

### 2.1 提交历史（`main` 分支）

```
7dff91d6  chore: point freeink-sdk submodule at the JRB OS fork
2bdd6dd  phase2: compress embedded CJK fonts 12/14/16pt with DEFLATE   (4,682,288 → 4,211,216)
6cb855d  phase1: drop settings rows for removed features
155bb0f  docs: rebrand as JRB OS; README + MIT attribution
1e84815  phase1 step3: remove network stack, OPDS, plugins, WeRead, KOSync  (5,424,224 → 4,683,504)
b3d8d0b  phase1 step2: delete src/activities/apps + WeRead reader integration (5,892,352 → 5,424,224)
fabbd3b  phase1 step1: i18n 33 langs → EN,ZH_CN                            (6,307,184 → 5,892,352)
1512e8e  ← 上游 CrossMux HEAD（基线）
```

### 2.2 子模块（**重要**）

`freeink-sdk` 已 fork 到 https://github.com/leo123d/freeink-sdk
主仓库 `.gitmodules` 指向该 fork，指针为 `6e7c7e5`。

**fork 里带了什么**：
1. `SDCardManager.cpp`：文件名缓冲 `char name[128]` → `512`
   （UTF-8 中文路径 3 字节/字，40 字加目录就溢出 128，会被静默截断 → **列出但打不开**；
   实测最长路径 206 字节）
2. `MetalioEInk4Board.h` 的一批板级支持更新 —— **这不是我加的**，是原始克隆时就存在的未提交改动

**⚠️ 已知不一致**：`MetalioEInk4Board.h` 在工作区仍是**未提交**的新版本（约 198 行），
而 fork 提交里是 125 行。交叉验证过：**指向 125 行版本也能构建成功**（该文件服务 Metalio 板，与 readpico 无关），
但两处版本不同这点应留意。

### 2.3 许可（合规要点）

- 项目是 **CrossMux 的衍生作品**，CrossMux 又是 **CrossPoint** 的分支
- `LICENSE` 是 **MIT，保留全部上游署名**（Dave Allie / FreeInk / EEGO / CrossPoint）+ 追加 JRB OS 声明
- 上游 README 保留为 `README.upstream.md` 与 `README.upstream.zh-CN.md`，**不要删**
- ⚠️ 仓库里有一份 `README.zh-CN.md` 仍是上游原文（含"CrossMux"字样），可考虑改名或替换

---

## 3. 交付物（可直接使用）

### 3.1 固件包 `F:\paperread\release\`

| 文件 | 大小 | SHA256（前 16 位） |
|---|---|---|
| `firmware.bin` | 4,211,216 B | `2B2203DFA24A52C0` |
| `firmware.factory.bin` | 4,276,752 B | — |
| `bootloader.bin` | 18,720 B | `15F88AE11793EDDC` |
| `partitions.bin` | 3,072 B | `BD0F7954ACA2EF7D` |
| `boot_app0.bin` | 8,192 B | `F94C5D786A7A8FAB` |

**回退固件**：`F:\paperread\baseline-firmware.bin`（6,307,184 B = 未改造的上游 CrossMux）

### 3.2 刷写方式（**必须整表刷写**）

分区表是**双槽 A/B**（`app0`/`app1` 各 6.25 MiB），与板厂出厂分区表**不同**：

```
nvs       data  nvs      0x9000    0x5000
otadata   data  ota      0xe000    0x2000
app0      app   ota_0    0x10000   0x640000   ← 6.25 MiB
app1      app   ota_1    0x650000  0x640000   ← 6.25 MiB
spiffs    data  spiffs   0xc90000  0x360000
coredump  data  coredump 0xFF0000  0x10000
```

```bash
# 1) 先完整备份 16 MB（必做）
esptool.py --chip esp32s3 --port COMx --baud 921600 read_flash 0 0x1000000 backup-full-16mb.bin

# 2) 整表刷写
esptool.py --chip esp32s3 --port COMx --baud 921600 write_flash \
  0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin

# 3) 拔掉 USB 再重新开机
```

> 完整说明与 12 项验收清单见 `FLASH.md`。

---

## 4. 已完成的改造（含实测数据）

### 4.1 体积进展（全部为实测，非估算）

| 步骤 | 固件大小 | 变化 |
|---|---|---|
| 上游基线 | 6,307,184 B | — |
| step1 i18n 33 语言 → EN+ZH_CN | 5,892,352 B | −414,832 |
| step2 删 apps 整树 + 阅读页 WeRead | 5,424,224 B | −468,128 |
| step3 删整个网络栈 | 4,683,504 B | −740,720 |
| settings 失效菜单行 | 4,682,288 B | −1,216 |
| **step4 压缩现有 CJK 字库** | **4,211,216 B** | **−471,072** |
| | | **累计 −2,095,968（−33.2%）** |

OTA 单槽 6,553,600 B → **余量 2,342,384 B**。

### 4.2 删除了什么

| 类别 | 内容 |
|---|---|
| 游戏 | 2048、五子棋、中国象棋、扫雷、推箱子、数独、木鱼、像素开关、计算器 |
| 应用 | AirPage 相册、丑头像、Buddy、待机屏、阅读统计页 |
| 微信读书 | 阅读页进度同步、书架提升、初始进度、`lib/WeReadWebApi` 整个库 |
| 界面语言 | 33 种 → **仅中文 + 英文** |
| 网络栈 | WiFi、Web 服务器、OPDS/Catalog、Calibre、WebDAV、插件系统、USB 挂载 |
| 联网 OTA | 改为 **SD 卡本地刷写**（`FirmwareFlasher` + `SdFirmwareUpdateActivity`） |
| KOReader 同步 | 需联网，一并移除 |
| 失效设置行 | WiFi/OPDS/插件/KOReader/词典下载/时钟/应用可见性 |

### 4.3 保留了什么（**不要误删**）

- ✅ EPUB 阅读引擎
- ✅ **EPUB 内容保护解密（本地 DRM）** —— 依赖 **wolfSSL**，见 §6 教训
- ✅ SD 卡本地 OTA 刷写（`src/ota/`：`FirmwareFlasher` / `OtaBootSwitch` / `FirmwareBoardTag` / `ProtectedPaths`）
- ✅ 本地字库（`.cpfont`）、字典、书签
- ✅ 中文显示（内嵌 3500 常用字 @12pt；大字号正文仍走 SD 卡字库）

---

## 5. 阶段 2 字库：已完成部分 + 关键约束

### 5.1 上游已经有内嵌 CJK 字库（**别重新发明**）

用 **linker map** 实测（唯一可信口径）：只有 3 个真正链接进固件。

> ⚠️ **绝不能用 `.h` 文件体积判断字库大小** —— 位图存为十六进制文本，文件大小约为真实字节的 **6 倍**。
> 唯一可信测量口径：`.pio/build/readpico/firmware.map`。

工具链在 `lib/EpdFont/scripts/`，**已可直接复用**：
`fontconvert.py`（`--2bit --compress --zopfli --characters --additional-intervals`）、
`build-cn-builtin-fonts.sh`、`build_cn_charset.py`、`share-cn-font-intervals.py`、**`verify_compression.py`**。

依赖齐全：`freetype-py`、`fontTools 4.66.1`、`zopfli`。

**源字体**：上游**不提交** `NotoSansSC-Regular.otf`，我们已有的副本在
`F:\paperread\font-probe\NotoSansSC-Regular.otf`（8,331,336 B）。
> FreeType **无法打开非 ASCII 路径下的字体** —— 生成必须在 ASCII 路径做（如 `F:\paperread\fontwork`）。

### 5.2 已完成：压缩现有 12/14/16pt（提交 `2bdd6dd`）

| 字体 | 原 Bitmaps | 压缩后 | glyph 数 | 省下 |
|---|---|---|---|---|
| `notosans_cjk_12` | 525,663 | 296,438 | 4,014（一致） | 198,733 |
| `notosans_cjk_14` | 190,951 | 89,212 | 1,300（一致） | 101,631 |
| `notosans_cjk_16` | 244,194 | 103,586 | 1,300（一致） | 140,482 |
| | | | | **440,846** |

固件实测 −471,072 B（比预测多省 30 KB，因为压缩后 interval 表也变小）。

**原始未压缩字体已备份**：`lib/EpdFont/builtinFonts/raw_backup/`（可 `git revert 2bdd6dd`）

### 5.3 ⚠️ 但这一项**必须真机验证**（最高优先级）

`build-cn-builtin-fonts.sh` 第 17–20 行明确写了**为什么上游故意不压缩 CJK**：

> 6 个 CJK 字体同时加载时每个 group 需约 50 KB scratch，会**碎片化堆**，
> 启动时让 `FontDecompressor` 抛 `std::bad_alloc` **崩溃**。

这是**真机现场崩溃**换来的教训（`FontDecompressor.h` 第 72–74 行记录了那次 `abort()`，当时仅剩约 11 KB 堆）。

**我为什么仍这么做**：当前所有解压缓冲都走 `fiFontMalloc`，而它 **PSRAM 优先（8 MB）**：

```c
// freeink-sdk/libs/font/FreeInkFont/src/FontAlloc.c:11
void* fiFontMalloc(size_t size) {
  void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);        // PSRAM 优先
  if (p == NULL) p = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
```

**但这是读代码的推论，未经真机证实。**

**验证清单**：

| # | 操作 | 看什么 |
|---|---|---|
| 1 | 开机 | 是否卡启动 / 反复重启 |
| 2 | **中文书连续翻 200 页** | 是否中途崩溃（内存碎片化） |
| 3 | **反复切字号** 12↔14↔16↔20 | 是否分配失败 |
| 4 | 进出阅读页 20 次 | 堆是否恢复 |
| 5 | 串口日志 | `bad_alloc` / `abort` / `FDC` 错误 / `Failed to allocate` |

`FontDecompressor` 内建 `peakTempBytes` 计数器（`.h` 第 42 行），日志里能看到就记下来。

**崩溃就回退**（`raw_backup/` 或 `git revert 2bdd6dd`）。

### 5.4 待做：完整 GB2312 一二级内嵌（实测数据）

设计目标 = GB2312 **一级+二级共 6,763 字**，三档 12/14/20pt，DEFLATE 压缩：

| 档位 | Bitmaps | Glyphs | Intervals | Groups | 总计 |
|---|---|---|---|---|---|
| 12pt | 571,347 | 101,640 | 42,696 | 342 | 716,025 |
| 14pt | 685,415 | 101,640 | 42,696 | 432 | 830,183 |
| 20pt | 1,028,412 | 101,640 | 42,696 | 792 | 1,173,540 |
| | | | | | **2,719,748** |

**空间测算**（结合已完成的压缩）：
```
当前固件 4,211,216 − 现有 CJK 字库 1,092,804 + GB2312 三档 2,719,748 = 5,838,160 B
发布门槛 6,029,312 B  →  余量 191,152 B  ✅ 放得下但偏紧
```

**更稳的做法**：只嵌 **12pt + 20pt 两档**（省 830,183 B）。

⚠️ **坑**：仓库里的 `gb2312_lv1.txt` **只是一级字（3,755 字）**，不是设计要的 6,763 字。
必须自己从 codec 生成：

```python
hanzi = []
for hi in range(0xB0, 0xF8):
    for lo in range(0xA1, 0xFF):
        try: ch = bytes([hi, lo]).decode("gb2312")
        except UnicodeDecodeError: continue
        if len(ch) == 1 and 0x4E00 <= ord(ch) <= 0x9FFF: hanzi.append(ch)
# -> 6763 unique
```

**关键结构事实**（压缩挤不掉的固定成本）：
```
EpdFontData = Bitmaps[] + EpdGlyph[] + EpdUnicodeInterval[] + EpdFontGroup[]
                 ↑ 可压缩     ↑ 14 B/字    ↑ 12 B/区间          ↑ 18 B/组
```
`EpdGlyph` = 14 B/字 × 6,763 = **94,682 B，每档一份，无法压缩、无法跨档共用**（三档共 304,920 B）。

详细数据与脚本清单见 **`FONT-STRATEGY.md`**。

---

## 6. ⚠️ 六条硬教训（重做会很贵）

### 6.1 不要把 wolfSSL / crypto 当网络代码一起删

我一度想删 wolfSSL（以为只服务网络）。**编译错误暴露真相：它还负责本地 EPUB 内容保护解密**
（`lib/Epub/BookKey.cpp`、`ContentProtection.cpp`）——**与联网无关**。

**连带问题**：wolfSSL 需要的 `wolfSSL_Arduino_Serial_Print` 符号原本定义在
`src/network/HttpDownloader.cpp`（随网络栈删掉了），导致链接失败。
**已补**在 `src/ota/WolfSslSerialPrint.cpp` —— **不要删这个文件**。

### 6.2 每完成一个可构建步骤就提交检查点

我曾一次性做三件大事且**未提交**，一个脚本错误导致半应用状态，
**已验证的绿状态在 git 里毫无记录**，只能从 stash 手工捞回。现在已建立逐提交检查点制度。

### 6.3 不要用多行正则删 C++ 代码

用 `re.sub` 删 `return activityManager.replaceActivityWith<X>(...)` 时只匹配首行，
**把多行语句截断**，留下孤儿代码。**删函数体请用花括号配对扫描。**

### 6.4 脚本里的路径常量要先验证

脚本里写 `src\settings`，真实路径是 `src\activities\settings` →
「删除文件」静默什么都没删（被 `os.path.exists` 挡住），而「剥离引用」却生效了 —— 一半成功一半失败。

### 6.5 PowerShell 会吞引号 / 终端是 GBK

- `python -c @"..."@` 会把引号弄乱 → **一律写成 `.py` 文件再执行**
- 控制台是 GBK，输出中文/符号会 `UnicodeEncodeError` → **务必 `$env:PYTHONIOENCODING = "utf-8"`**

### 6.6 不能用源文件体积估算固件体积

i18n 实测 **4.9 : 1**（源文件减 1.97 MB，固件只减 405 KB —— 链接器本来就在丢弃未引用字符串）。
**每步必须实测 `firmware.bin`。**

---

## 7. 本机环境专属配置（换机器需重建）

### 7.1 git 推送走 SSH-over-443（因为 `github.com:443` 被阻断）

`%USERPROFILE%\.ssh\config`：
```
Host github.com
  HostName ssh.github.com
  Port 443
  User git
  IdentityFile ~/.ssh/jrbos_ed25519
  IdentitiesOnly yes
  StrictHostKeyChecking accept-new
```

密钥：`~/.ssh/jrbos_ed25519`，指纹 `SHA256:aFWnYBD+AvP0ycD0rhSiW/FNI7jHTamJu8BlEIjEjhY`
（已注册到 GitHub 账号 `leo123d`）

远端配置：
```
origin    git@github.com:leo123d/JRB-OS.git          ← SSH
upstream  https://github.com/0x1abin/crossmux.git    ← HTTPS（此网络不通）
upstream-ssh  git@github.com:0x1abin/crossmux.git    ← SSH（可用来 unshallow）
```

> **注意**：原克隆是 `--depth=1` 浅克隆，推送时会报
> `remote: fatal: did not receive expected object` —— 必须先补全历史：
> `git fetch --unshallow upstream-ssh`（已做过，现为 2,093 个提交）

### 7.2 其它

- `F:\scons-tool\` — 完整的 SCons 4.11.1 发行版（排查 tool-scons 时下载的，可留可删）
- `F:\paperread\fontwork\` — 字库生成工作目录（含 GB2312 完整字符集与压缩产物）
- `F:\小纸pico\.crosspoint\` — **用户 SD 卡数据的副本**（见 §8）
- `F:\小纸pico\.paperread-tools\` — venv（PlatformIO + freetype-py + fontTools + zopfli）

---

## 8. 阶段 3 缺陷状态

### 8.1 B1 中文文件名乱码 —— **判定：不可复现**（详见 `B1-FINDINGS.md`）

逐条验证：

| # | 检查 | 结果 |
|---|---|---|
| 1 | `recent.json` 本身 | ✅ **是正确的 UTF-8**（`寻` = `e5 af bb`，无 BOM） |
| 2 | `瀵昏抗` 来源 | ✅ 是**读取工具用 GBK 解码 UTF-8** 的假象（`'瀵昏抗'.encode('gbk').decode('utf-8') == '寻迹'`） |
| 3 | FAT 长文件名配置 | ✅ `USE_UTF8_LONG_NAMES=1` 在 `platformio.ini` 两处 + `inject_build_flags.py` 的 `_ALWAYS` |
| 4 | 目录扫描/文件名处理 | ✅ UTF-8 安全（路径左截断显式跳过续字节；`formatFileName` 字节前缀终止于字符边界） |
| 5 | Unicode 归一化 | ✅ 8 条真实路径**全部 `NFC == NFD`**，无陷阱 |

**唯一发现的残留隐患**（已修）：`SDCardManager::listFiles` 的 `char name[128]` → 512。

**⚠️ 未能完成**：无法做「设备列出的名字 ↔ 卡上真实字节」的直接比对（SD 卡未挂在主机上）。

**建议**：**不要按"乱码"去改代码**。向用户确认真实症状：
- (a) 文件名显示乱码 → 查**渲染/字形缺失**（内嵌 12pt 只有 3500 常用字，生僻字渲染为方框，易被误认为乱码）
- (b) 中文书名打不开 → 查 FAT / 文件名长度
- (c) 只是从电脑上看 `recent.json` 时乱码 → **不是 bug**

### 8.2 B2 CSS 内存不足静默降级 —— ⬜ **未动**

现象：CSS 解析器需要 **65,536 B 连续内存**，拿不到就静默跳过样式。
方向：改为两遍流式解析，峰值 ≤ 16 KB。

### 8.3 B3 横屏页脚裁切 + 5× `GFX Outside range` —— ⬜ **未动**

### 8.4 其它已知未做

- ⬜ 首页 Tab 体系彻底收敛（部分 Tab 已无内容）
- ⬜ `I18nKeys.h` 中已无用的键清理（WeRead 相关代码已全部移除，仅剩注释）
- ⬜ `lib/EdpFont/builtinFonts` 里未链接的死字体可从**仓库**删除（不影响固件，只减小仓库体积）

---

## 9. 设计决策（八项，来自 `PAPERREAD-DESIGN.md`）

| # | 决策 | 状态 |
|---|---|---|
| ① | 正文默认 20pt | ⬜ 需字体支持 |
| ② | 取消 Tab 导航 | 🟡 首页已收敛为 3 项；Tab 体系未尽 |
| ③ | 不保留微信读书 | ✅ 已完成 |
| ④ | 段落级划线批注 | ⬜ |
| ⑤ | 拾取唤醒（SC7A20H 加速度计） | ⬜ |
| ⑥ | 删 USB 挂载与 WiFi，传书靠人为拷贝 | ✅ 已完成 |
| ⑦ | UI 仅三项入口 | 🟡 首页菜单已是 3 项 |
| ⑧ | 时间与日期全删、统计不按天 | 🟡 时钟 UI 已移除；`TimeUtils`/统计存储未动 |

**硬件真实瓶颈**：不是 Flash，是**内存**。
Flash 16 MiB（A/B 双槽各 6.25 MiB）／PSRAM 8 MiB／
**SRAM 512 KB，可用内部堆仅 ~30–45 KB**。
全刷一页 684×1216 16 级灰度 ≈ 416 KB —— 排版、CSS 解析、字库缓存全挤在几十 KB 里。

---

## 10. 关键文件位置

### 10.1 文档（`F:\小纸pico\`）

| 文件 | 内容 |
|---|---|
| **`HANDOVER.md`** | **本文档** |
| `PAPERREAD-DESIGN.md` | 设计文档 v1.5+（185 KB，八项决策 + 全部设计细节） |
| `BUILD.md` | 构建环境全部踩坑与可复现步骤 |
| `FLASH.md` | 刷写说明 + 12 项真机验收清单 |
| `FONT-STRATEGY.md` | 字库实测数据、方案对比、待验证清单 |
| `PHASE1-LOG.md` | 阶段 1 逐步实测记录 + 检查点表 |
| `PHASE1-PLAN.md` | 阶段 1 原始执行清单 |
| `B1-FINDINGS.md` | B1 调查全过程与结论 |

### 10.2 代码

```
F:\paperread\crossmux\               仓库（分支 paperread-phase1 = main）
  src/ota/                           SD 卡本地 OTA（不含 WolfSslSerialPrint.cpp 就链接失败）
  lib/EpdFont/scripts/               字库工具链
  lib/EpdFont/builtinFonts/          内嵌字体（raw_backup/ = 压缩前的原始版本）
  freeink-sdk/                       子模块（fork，指向 leo123d/freeink-sdk @ 6e7c7e5）
F:\paperread\release\                固件包
F:\paperread\baseline-firmware.bin   回退用固件
F:\paperread\fontwork\               字库生成工作目录
```

### 10.3 分析脚本（`F:\paperread\*.py`，36 个）

| 脚本 | 用途 |
|---|---|
| `measure_gb2312_full.py` | **GB2312 一二级完整字符集生成 + 三档压缩实测** |
| `compress_cjk.py` | 压缩现有 12/14/16pt CJK 字体（产出到 `fontwork/cjkcomp/`） |
| `font_size_true.py` | 从头文件算真实字节数（Bitmaps/Glyphs/Intervals/Groups） |
| `measure_cjk_compress.py` | 压缩收益对比测量 |
| `install_cjk_fonts.py` | 把压缩字体装入仓库（自动备份原始版本） |
| `b1_probe.py` | 穷举乱码反向变换 |
| `b1_nfc_check.py` | 真实路径 NFC/NFD 敏感性检测 |
| `strip_*.py` / `fix_*.py` / `del_*.py` | 阶段 1 各步的删除与引用清理（可参考其做法） |
| `repro_scons.py` | tool-scons 故障隔离复现 |

---

## 11. 下一步建议（按优先级）

### 🔴 P0：真机验证压缩字库（阻塞后续一切）

这是当前最大的未知。若崩溃，字库路线要整体重来。

```bash
# 刷 F:\paperread\release\firmware.bin（整表刷写，见 §3.2）
# 重点：开机、连续翻 200 页中文、反复切字号、看串口日志
```

### 🟠 P1：修 B2（CSS 内存不足）

它直接影响阅读排版质量，且技术路径清晰（两遍流式解析，峰值 ≤ 16 KB）。

### 🟠 P1：完整 GB2312 内嵌（12pt + 20pt 两档最稳）

让中文正文不再依赖 SD 卡字库。用 `measure_gb2312_full.py` 生成，注意 §5.4 的字符集陷阱。

### 🟡 P2：首页 Tab 体系收敛（决策 ②⑦）

### 🟡 P2：阅读页 UI 重写（零装饰 + 4 键工具栏）

⚠️ **桌面模拟器在 Windows 跑不通**，UI 迭代只能靠真机，效率很低。
**建议先在 workbuddy 环境把模拟器跑起来**——这可能比修任何单个 bug 的收益都大。

---

## 12. 转到 workbuddy 时的检查清单

- [ ] 克隆仓库 + 子模块：`git clone --recurse-submodules git@github.com:leo123d/JRB-OS.git`
- [ ] **跑一次平台补丁**：`python scripts/patch_pioarduino_cache.py --prepare-platform`
- [ ] 装依赖：PlatformIO + `freetype-py` + `fontTools` + `zopfli`（字库工作用）
- [ ] 设 `PYTHONIOENCODING=utf-8`
- [ ] 构建验证：`pio run -e readpico` 应得 **4,211,216 B**
- [ ] 拷贝源字体 `NotoSansSC-Regular.otf`（8.3 MB，上游不提交）
- [ ] 拷贝固件包与回退固件（若需要）
- [ ] **尝试把桌面模拟器跑起来**（Linux 下装 SDL2 开发库，且网络能拉 `crosspoint-simulator`）
- [ ] 通读 §6 六条硬教训 —— 每条都是实际踩出来的

---

## 13. 诚实声明

**已验证的**：所有固件体积数字、字库压缩实测、构建可复现、GitHub 推送、B1 的逐条检查结论。

**未验证的**（不要当成事实）：
- ⚠️ **固件从未在真机上跑过** —— 一切运行时行为未知
- ⚠️ 压缩 CJK 字库在真机上是否稳定（§5.3，有历史崩溃先例）
- ⚠️ B1 是否真的不存在（无法做设备端与卡上字节的直接比对）
- ⚠️ `MetalioEInk4Board.h` 两个版本并存的影响（已验证 125 行版本也能构建，但未深究）

**我的错误已纠正记录在案**（避免重蹈）：
1. 曾据 `.cpfont` 路径误判"三档字库不可行"→ 实际走 C 头文件路径可行
2. 曾据 `gb2312_lv1.txt`（仅一级字）误判"压缩能省 1 MB"→ 完整字符集下省约 1.55 MB 原始/实际三档 2.72 MB
3. 曾误删 wolfSSL → 差点破坏本地 DRM 解密
4. 曾把 B1 当成确证 bug → 实为读取假象
