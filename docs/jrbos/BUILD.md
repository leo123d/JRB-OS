# BUILD.md — 纸读 PaperRead 构建环境说明

> 记录本机（Windows 11 / Python 3.12.14）搭建 `crossmux` readpico 构建环境的**实测结论与踩坑**。
> 目的：让下一次构建不必重走弯路。
> 最后更新：2026-10-06

---

## 1. 已就绪的资产

| 项 | 路径 | 说明 |
|---|---|---|
| 源码仓库 | `F:\paperread\crossmux` | **纯 ASCII 路径**（见 §3 教训） |
| 子模块 | `F:\paperread\crossmux\freeink-sdk` | `96de1be6`，含 `BoardReadPico` + `EpdiyLcd` |
| Python venv | `F:\小纸pico\.paperread-tools` | PlatformIO 6.2.0 + fontTools + freetype-py + Pillow |
| 字体生成器 | `F:\paperread\crossmux\lib\EpdFont\scripts\fontconvert_sdcard.py` | 上游原版，直接用 |
| 测试字体 | `F:\paperread\font-probe\NotoSansSC-Regular.otf` | 8,331,336 B |
| 区间规格 | `F:\paperread\font-probe\intervals_exact.txt` | GB2312 精确区间，3,551 个区间 / 7,594 码位 / 零膨胀 |
| SCons 兜底副本 | `F:\paperread\scons-stable` | SCons 4.11.1，供 `sys.path` 兜底 |
| 字库实测产物 | `F:\paperread\font-probe\out-exact\` | 12/14/20pt 三个 `.cpfont` |
| 设计文档 | `F:\小纸pico\PAPERREAD-DESIGN.md` | v1.5+，含实测修正 |

---

## 2. 构建命令

> ✅ **2026-10-06：构建已跑通。** 首次必须做一次性的 §2.0 平台补丁，否则必然失败。

### 2.0 一次性前置：应用平台补丁（**关键，漏了就必然失败**）

```powershell
$env:PYTHONIOENCODING = "utf-8"
Set-Location "F:\paperread\crossmux"
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts\patch_pioarduino_cache.py --prepare-platform
```

**为什么必须做**：这个补丁把 `"tool-scons"` 从 pioarduino 平台的 `COMMON_IDF_PACKAGES` 列表里移除，从而**阻止平台的包安装器覆盖 PlatformIO 核心正在使用的 SCons**。
⚠️ **上游只在 CI 工作流里调用它**（`.github/workflows/{ci,hardware-ci,nightly,sync-build}.yml`），**本地开发文档里没有这一步** —— 这就是本次卡了 3 轮的根因。
补丁幂等，可重复执行。它会把平台 `platform.py` 改成：

```python
COMMON_IDF_PACKAGES = [
    "tool-cmake",
    "tool-ninja",
    # "tool-scons",   ← 被移除
    "tool-esp-rom-elfs"
]
```

### 2.1 构建固件

```powershell
$env:PYTHONIOENCODING = "utf-8"      # 必要：上游 i18n 生成会打印阿拉伯语行，Windows 默认 cp936 会卡住

& "F:\小纸pico\.paperread-tools\Scripts\pio.exe" run -e readpico -d "F:\paperread\crossmux"
# 或用系统那套：
# & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e readpico -d "F:\paperread\crossmux"
```
> 两套 pio 共用同一个 `C:\Users\lee\.platformio` 包缓存，效果相同。
> 冷编译约 **5 分钟**（870 个目标文件）。

### 2.2 桌面模拟器（**尚未跑通，两个独立障碍**）

```powershell
pio run -e simulator_readpico -d F:\paperread\crossmux
```

| 障碍 | 状态 |
|---|---|
| 依赖 `crosspoint-simulator` 需从 GitHub 拉取（pin: `58b6694d`） | ❌ 当前网络 `Connection was reset` / 连不上 443 |
| `build_flags` 含 `!sdl2-config --cflags --libs` | ❌ **Windows 没有 `sdl2-config`**，那是 Linux/macOS 工具；需装 SDL2 开发库并自建 shim |

---

## 3. 教训一：路径必须纯 ASCII

**实测**：项目在 `F:\小纸pico\...` 下时，FreeType **无法打开字体文件**：
```
freetype.ft_errors.FT_Exception: FT_Exception: (cannot open resource)
```
同一文件复制到 `F:\paperread\...` 后立即可读（`family: b'Noto Sans SC'`）。

**结论**：**仓库与字体必须放在纯 ASCII 路径下。** 中文路径（如 `小纸pico`）会让 FreeType / 部分 Python C 扩展打不开文件。

---

## 4. 教训二：本机 PlatformIO 包管理器故障（阻塞构建，未解决）

### 4.1 症状

```
*** [.pio\build\readpico\firmware.elf] ModuleNotFoundError : No module named 'SCons.Tool.FortranCommon'
```
- 报错在**链接阶段**（`firmware.elf`），编译尚未开始 → **0 个目标文件**。
- `SCons.Tool.FortranCommon` 由 `linkCommon/__init__.py` 惰性导入，所以只在链接期暴露。
- **不是缺文件**：`FortranCommon.py` 存在于 scons-local 包里。

### 4.2 定位到的事实

| 事实 | 证据 |
|---|---|
| 台机器上 `C:\Users\lee\.platformio\packages\tool-scons` **原本缺失** | 该目录不存在，但 PlatformIO 运行时引用它 |
| `pio run` 每次都会**删除并重装** `tool-scons` | 构建前存在 → 构建后消失；日志有 `Tool Manager: Installing platformio/tool-scons @ ~4.41101.0` → `has been installed!` |
| 重装**实际失败** | 日志报"已安装"，但目录为空 |
| 手动 `pio pkg install -g -t platformio/tool-scons@4.41101.0` 能装成功 | 装完后 `FortranCommon.py` 存在、可 import |
| 但下一次 `pio run` 又把它删掉 | 复现 6 次以上 |
| **改 `PLATFORMIO_CORE_DIR` 到 F: 也不行** | 因为 `packages` 仍经 junction 指向 `C:\Users\lee\.platformio` |
| 平台声明的是 **SCons 4.8.1**，PlatformIO 实际要 **4.41101.0（=4.11.1）** | `platform.json` 的 `tool-scons` 版本 = `4.8.1`；日志要 `~4.41101.0` |

### 4.3 已尝试且无效的办法

1. 装 SCons 4.11.1（版本对）→ 仍被删
2. 装平台声明的 SCons 4.8.1 → 仍被删
3. 装进 F: 盘 core dir（F: 解压正常）→ 仍被删，因为 packages 经 junction 指向 C:
4. 用 junction 把 `tool-scons` 指向真实目录 → PIO 的 `copytree` 报 `FileExistsError`
5. 在 venv 的 `site-packages` 放 `.pth` → SCons 进程不读 venv 的 site-packages
6. 在 `~/.platformio/penv` 的 `site-packages` 放 `.pth` → SCons 进程的 `sys.path` 由 PlatformIO 显式构造，不吃 `.pth`

### 4.4 当前可用的绕过

**手工执行一次，然后立刻构建**（同一个命令内）：
```powershell
$env:PLATFORMIO_CORE_DIR = "F:\paperread\pio-core"
$env:PYTHONIOENCODING = "utf-8"
$pio = "F:\小纸pico\.paperread-tools\Scripts\pio.exe"
& $pio pkg install -g -t "platformio/tool-scons@4.41101.0"
& $pio run -e readpico -d "F:\paperread\crossmux"
```
**注意**：`pio run` 仍会在启动时删掉它，所以这个办法**实测也失败**。见 §4.5。

### 4.5 建议的下一步（未验证）

按猜测成功率排序：

1. **用 `robocopy` 把整个 `C:\Users\lee\.platformio\packages` 复制到 F: 盘**，让 CORE_DIR 完全自洽（不再有 junction）。约 5–8 GB、5–15 分钟。这是最可能成功的方案——绕开"C: 上新建目录被丢弃"这个未解释的现象。
2. **核对 Python 版本要求**：`platform.py` 要求 Python `3.10–3.14`。它自带 `tool-esptoolpy`，venv 用的是 DSH 的 Python 3.12.14，**应无问题**，但值得用 `~/.platformio/penv` 的 Python 再试一次。
3. **官方路径复现**：在你的机器上（而非 DSH 会话内）跑一次 `pio run -e readpico`。上面的失败模式带有"沙箱内写 `C:\Users\lee\.platformio` 被丢弃"的特征，**在正常 shell 里很可能不会复现**。

### 4.6 ✅ 根因与解法（2026-10-06 已解决）

**根因：漏了一步只能从上游 CI 配置里发现的平台补丁。**

#### 真正的故障链

`pio run` 要装**两个互相冲突的 `tool-scons` 版本，且目标目录相同**：

| 来源 | 版本 | 内容 |
|---|---|---|
| **espressif32 平台**（`platform.json`） | `tool-scons@4.40801.0`（SCons **4.8.1**） | 从 pioarduino registry 的 `scons-4.8.1.zip` 取，**但那是个 2 KB 索引**（只有 `package.json` + `tools.json`）→ **没有 `scons.py` / `SCons/`** |
| **PlatformIO 核心**（`dependencies.py`） | `~4.41101.0`（SCons **4.11.1**） | 从 `scons-local-4.11.1.tar.gz` 取，**这是正确完整的包** |

两者都写 `packages\tool-scons` → 互相覆盖。而 `_run_scons`（`platformio/platform/_run.py:71`）执行的是：

```python
scons_dir = get_core_package_dir("tool-scons")
args = [python, os.path.join(scons_dir, "scons.py"), ...]
```

**目录一被平台的 stub 覆盖，`scons.py` 就没了 → SCons 根本起不来。**
（`FortranCommon` 只是最先被导入的 Tool 模块，所以成了报错点——**报错信息有误导性**，真问题是整个 SCons 包被换成了垃圾。）

#### 观测证据（目录监听）

```
 0.0s  ABSENT
 1.6s  files=222 scons.py=True    ← 核心版装好
 2.1s  ABSENT                     ← 被平台的 stub 覆盖流程清掉
 3.1s  files=288 scons.py=True    ← 再装好
 9.7s  files=81  scons.py=True    ← 被部分删除
10.2s  ABSENT                     ← 彻底没了，而 SCons 要到 40+ 秒才启动
```

#### 解法

```powershell
Set-Location F:\paperread\crossmux
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" scripts\patch_pioarduino_cache.py --prepare-platform
```

补丁把 `"tool-scons"` 从平台的 `COMMON_IDF_PACKAGES` 移除，平台安装器就不再碰它。
**修复后立即构建成功**：

```
Tool Manager: Installing .../scons-local-4.11.1.tar.gz
Tool Manager: tool-scons@4.41101.0 has been installed!
Compiling .pio\build\readpico\lib6c7\Wire\Wire.cpp.o
...
Successfully created ESP32-S3 image.
======================== [SUCCESS] Took 292.04 seconds ========================
readpico       SUCCESS   00:04:52.038
```

#### 为什么卡了 3 轮

上游**只在 CI 工作流里调用这个补丁**（`ci.yml:73`、`hardware-ci.yml:133`、`nightly.yml:127`、`sync-build.yml:57`），
`README` / `docs/` 的本地开发步骤里**完全没有提**。而且：

- ❌ **不是沙箱问题**（在原生 shell 里同样失败 —— 我曾误判为主要怀疑方向）
- ❌ **不是磁盘空间**（C: 有 62.9 GB 空闲）
- ❌ **不是包缺失**（`pio pkg install -g -t platformio/tool-scons@4.41101.0` 能装出正确的 222 文件包，且 `import SCons.Tool.FortranCommon` 实测成功）
- ❌ **报错信息有误导性**（`FortranCommon` 只是表象）

#### 走过但无效的 8 条路（留作记录，别再试）

| # | 尝试 | 为什么无效 |
|---|---|---|
| 1 | 装核心要的 4.11.1 | 会被平台 stub 再覆盖 |
| 2 | 装平台声明的 4.8.1 | 那个 zip 是索引，没有 SCons |
| 3 | 装完整 `scons-local`（含根级 `scons.py`） | 同上被覆盖 |
| 4 | `PLATFORMIO_CORE_DIR` 改到 F: | 冲突在包内容层，与位置无关 |
| 5 | junction 指向真实目录 | PIO `copytree` 报 `FileExistsError` |
| 6 | 加 `.piokeep` | 该逃生阀只管卸载路径，管不住重装 |
| 7 | venv / penv 放 `.pth` | SCons 子进程 `sys.path` 由 `scons.py` 自建，不吃 `.pth` |
| 8 | 复制 5.8 GB 包缓存到 F: | 与位置无关；跑到 8% 已停 |

---

### 4.7 阶段 0 基线数据（2026-10-06）

```
firmware.bin          6,307,184 B  (6.02 MB)     ← 未改造的 CrossMux readpico
firmware.factory.bin  6,372,720 B  (6.22 MB)
bootloader.bin           18,720 B
partitions.bin            3,072 B

OTA 单槽              6,553,600 B  (6.25 MiB)
剩余                    246,416 B  (241 KB)   ❌ 低于 512 KiB 门槛
```

**结论：原版已经几乎没有余量，连门槛都过不去** —— 印证设计文档"减法回收 Flash 是必需而非可选"的判断。
同时字库（三档压缩 2.68 MB）也**必须**靠减法腾出空间。

编译规模：870 个目标文件，编译产物约 296.9 MB。**冷编译约 5 分钟**（评估回归成本用）。

> ⚠️ **重要：不要在构建不通的状态下做阶段 1 的大规模删除**——否则无法区分"我删错了"和"环境坏了"。

---

## 5. 教训三：字体文件必须实测，不能估算

**症状**：设计文档按"每 1000 字 ≈ 380 KB"与"墨迹框 96 B/字"两个口径估算 20pt 字库约 714 KB。

**实测**：**2,879,274 B（2,812 KB）——差 3.9 倍。**

原因：字形位图按 FreeType 的 **em-box** 存（含 bearing 与行距），**不是**按墨迹裁剪框。20pt 实测约 **360–380 B/字**。

**由此得出的规则**：
> **字库体积只能实测。任何按字数的经验公式都不可靠。**
> 生成器会在 stderr 打印 `Bitmap: N bytes` 与 `Total: N bytes`，**以那个为准**。

---

## 6. 字体生成的正确用法

### 6.1 区间规格必须"精确不合并"

`fontconvert_sdcard.py` 会**为区间内每个码位生成字形**（缺失的存 0 长字形）。所以：

| 合并容差 | 区间数 | 覆盖码位 | 20pt 实测 |
|---|---|---|---|
| **0（仅合并真正连续）** | 3,551 | **7,594** | **2,879,274 B** ✅ |
| ≤64 | 21 | 19,717 | 7,742,094 B ❌（2.7 倍） |

**结论**：**用 `intervals_exact.txt`（容差 0）**。合并区间是**严重负优化**——索引省下的几百字节，换来上万个多余字形。

### 6.2 命令行长度限制的处理

`intervals_exact.txt` 约 60 KB，**超出 Windows 命令行上限**，直接传参会报：
```
程序"python.exe"无法运行: The filename or extension is too long
```
**解法**：用一个包装脚本读文件后设置 `sys.argv`，再用 `runpy` 调用生成器。
`sys.path` 必须包含生成器所在目录（它 `import cpfont_version`）。参见 `F:\paperread\font-probe\run_gen.py`。

### 6.3 生成命令（已实测可用）

```powershell
& "F:\小纸pico\.paperread-tools\Scripts\python.exe" "F:\paperread\font-probe\run_gen.py"
```
产出：`F:\paperread\font-probe\out-exact\PaperReadGB_{12,14,20}.cpfont`
耗时约 **6 秒**（三档）。

---

## 7. 字库体积实测汇总

用 **Noto Sans SC Regular** + `intervals_exact.txt` + 上游生成器：

### 7.1 结论（最重要）

**固件内嵌字库必须走 `fontconvert.py`（C 头文件路径）；不要用 `.cpfont` 的体积推断内置体积。**

| | `.cpfont`（SD 卡） | **内置 C 头文件（固件）** |
|---|---|---|
| 字形选择 | 区间内每码位都要字形 | **`--characters`：只导出指定字符** |
| 压缩 | ❌ 无 | ✅ **`--compress`（DEFLATE）**，收益 2.4–2.5× |
| 设备支持 | — | ✅ 生产已验证（上游 `notosans_12/14/16/18_*` 全为 `compressed: true`） |

### 7.2 实测数字

**`.cpfont` 路径**（`fontconvert_sdcard.py`，精确区间，7,373 字形）：

| 档位 | `.cpfont` |
|---|---|
| 12pt | 1,200,775 B |
| 14pt | 1,586,843 B |
| 20pt | 2,879,274 B |
| 三档 | **5,666,892 B（5.4 MB）** ❌ 放不下 |

**内置 C 头文件路径**（`fontconvert.py --2bit --characters … --compress`）：

| 档位 | 位图 | glyph 表 | interval | groups | **合计** |
|---|---|---|---|---|---|
| 12pt | 578,253 | 103,236 | 43,092 | 360 | **724,941 B** |
| 14pt | 654,698 | 103,236 | 43,092 | 468 | **801,494 B** |
| 20pt | 1,006,222 | 103,236 | 43,092 | 828 | **1,153,378 B** |
| **三档** | | | | | **2,679,813 B（2.56 MB）** ✅ |

**结构体大小**（`lib/EpdFont/EpdFontData.h`，全部 packed）：
- `EpdGlyph` = **14 B**：`width` u8, `height` u8, `advanceX` u16, `left` i16, `top` i16, `dataLength` u16, `dataOffset` u32
- `EpdUnicodeInterval` = **12 B**：`first` u32, `last` u32, `offset` u32
- `EpdFontGroup` = **18 B**：`compressedOffset` u32, `compressedSize` u32, `uncompressedSize` u32, `glyphCount` u16, `firstGlyphIndex` u32

> **glyph 表固定 103 KB/档**（7,374 × 14 B），**每个字号一份，不能跨档共享**。

### 7.3 ⚠️ 陷阱：不要用 `.h` 文件大小判断体积

位图存为**十六进制文本**（`0xCD, 0x5B, 0x31, …`）：

| 文件 | 文本体积 | **真实字节** | 比值 |
|---|---|---|---|
| `notosanssc_gb_20_z.h` | 6,880,286 B | **1,153,378 B** | 6.0× |
| `notosanssc_gb_20.h` | 17,677,792 B | 2,861,313 B | 6.2× |

**正确做法**：读 `static const uint8_t <name>Bitmaps[N]` 的 **N**，再加 glyph/interval 结构体计数。见 `hsize2.py`。

### 7.4 覆盖率缺口

GB2312 有 **7,594** 个码位（含标点/全角），但 **Noto Sans SC 只有 7,373 个有字形**（差 **221** 个）→ 设备上会缺字。
对策（设计文档附录 D-2）：构建期扫描用户实际藏书，定向补入高频缺字。

> 注：7,373 < 7,594 **不是 bug**，是字体本身的覆盖限制。

### 7.5 相关脚本

| 脚本 | 用途 |
|---|---|
| `mkintervals.py` | 由 GB2312 生成精确区间规格 |
| `gen_builtin.py` | 生成内置 C 头文件（plain + compressed，三档） |
| `hsize.py` / `hsize2.py` | 统计真实嵌入字节（绕开十六进制文本假象） |
| `run_gen.py` | 调 `.cpfont` 生成器，规避 Windows 命令行长度上限 |
| `tradeoff.py` | 区间合并容差 vs 覆盖码位 vs 索引字节 的权衡曲线 |

（均位于 `F:\paperread\font-probe\`）
